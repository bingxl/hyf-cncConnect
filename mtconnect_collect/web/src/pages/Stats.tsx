import { useEffect, useMemo, useState } from 'react'
import type { ReactNode } from 'react'
import { Card, Col, DatePicker, Empty, Progress, Row, Segmented, Select, Statistic, Table, Tag } from 'antd'
import dayjs, { type Dayjs } from 'dayjs'
import * as echarts from 'echarts'
import {
  ALL_MACHINES,
  apiMachines,
  apiMachining,
  apiProduction,
  apiProducts,
  apiSummary,
  type ProductItem,
  type SummaryItem,
  type TimePoint,
} from '../api'
import EChart from '../components/EChart'
import { bucketTime, fmtDur, pct } from '../format'

const { RangePicker } = DatePicker

const RANGES = [
  { key: 'today', label: '今日', range: (): [Dayjs, Dayjs] => [dayjs().startOf('day'), dayjs()] },
  {
    key: 'yesterday',
    label: '昨日',
    range: (): [Dayjs, Dayjs] => [dayjs().subtract(1, 'day').startOf('day'), dayjs().startOf('day')],
  },
  { key: '24h', label: '近24小时', range: (): [Dayjs, Dayjs] => [dayjs().subtract(24, 'hour'), dayjs()] },
  { key: '7d', label: '近7天', range: (): [Dayjs, Dayjs] => [dayjs().subtract(7, 'day'), dayjs()] },
  { key: 'month', label: '本月', range: (): [Dayjs, Dayjs] => [dayjs().startOf('month'), dayjs()] },
]

export default function Stats() {
  const [machines, setMachines] = useState<string[]>([])
  const [machine, setMachine] = useState<string>(ALL_MACHINES)
  const [range, setRange] = useState<[Dayjs, Dayjs]>([
    dayjs().subtract(24, 'hour'),
    dayjs(),
  ])
  const [machining, setMachining] = useState<TimePoint[]>([])
  const [production, setProduction] = useState<TimePoint[]>([])
  const [products, setProducts] = useState<ProductItem[]>([])
  const [summary, setSummary] = useState<SummaryItem[]>([])
  const [bucket, setBucket] = useState(1800)

  useEffect(() => {
    apiMachines()
      .then((r) => setMachines(r.machines.map((m) => m.name)))
      .catch(console.error)
  }, [])

  useEffect(() => {
    if (!machine) return
    const from = range[0].unix()
    const to = range[1].unix()
    apiSummary(from, to)
      .then((r) => setSummary(r.items))
      .catch(console.error)
    apiMachining(from, to, machine, bucket)
      .then((r) => setMachining(r.points))
      .catch(console.error)
    apiProduction(from, to, machine, bucket)
      .then((r) => setProduction(r.points))
      .catch(console.error)
    if (machine === ALL_MACHINES) {
      setProducts([])
    } else {
      apiProducts(from, to, machine)
        .then((r) => setProducts(r.items))
        .catch(console.error)
    }
  }, [bucket, machine, range])

  // 加工与产量按 bucket_ts 对齐合并，避免两个接口返回的桶不一致导致图表错位
  const buckets = useMemo(() => {
    const map = new Map<number, TimePoint>()
    for (const p of machining) map.set(p.bucket_ts, p)
    for (const p of production) {
      const e = map.get(p.bucket_ts)
      map.set(p.bucket_ts, {
        bucket_ts: p.bucket_ts,
        mach_sec: e?.mach_sec ?? 0,
        machining_count: e?.machining_count ?? 0,
        sample_count: e?.sample_count ?? 0,
        produced: p.produced,
        start_total: p.start_total,
        end_total: p.end_total,
      })
    }
    return [...map.values()].sort((a, b) => a.bucket_ts - b.bucket_ts)
  }, [machining, production])

  const labels = useMemo(() => buckets.map((p) => bucketTime(p.bucket_ts)), [buckets])
  const maxMach = buckets.reduce((a, b) => Math.max(a, b.mach_sec), 0)
  const machUnit = maxMach >= 3600 ? 3600 : 60
  const machUnitName = maxMach >= 3600 ? '小时' : '分钟'
  const machData = useMemo(
    () => buckets.map((b) => +(b.mach_sec / machUnit).toFixed(1)),
    [buckets, machUnit],
  )

  const machOption: echarts.EChartsOption = useMemo(
    () => ({
      tooltip: { trigger: 'axis' },
      grid: { left: 60, right: 30, top: 30, bottom: 60 },
      xAxis: { type: 'category', data: labels, axisLabel: { rotate: 45 } },
      yAxis: { type: 'value', name: machUnitName, minInterval: 1 },
      series: [
        {
          name: '加工时间',
          type: 'bar',
          data: machData,
          itemStyle: { color: '#1677ff' },
        },
      ],
    }),
    [labels, machData, machUnitName],
  )

  const prodOption: echarts.EChartsOption = useMemo(
    () => ({
      tooltip: { trigger: 'axis' },
      grid: { left: 50, right: 30, top: 30, bottom: 60 },
      xAxis: { type: 'category', data: labels, axisLabel: { rotate: 45 } },
      yAxis: { type: 'value', name: '件', minInterval: 1 },
      series: [
        {
          name: '产量',
          type: 'line',
          data: buckets.map((p) => p.produced ?? 0),
          smooth: true,
          areaStyle: { opacity: 0.15 },
          itemStyle: { color: '#fa8c16' },
        },
      ],
    }),
    [buckets, labels],
  )

  const totalMach = useMemo(() => buckets.reduce((a, b) => a + b.mach_sec, 0), [buckets])
  const totalProd = useMemo(
    () => buckets.reduce((a, b) => a + (b.produced ?? 0), 0),
    [buckets],
  )
  const totalPower = useMemo(
    () => summary.reduce((a, b) => a + b.power_sec, 0),
    [summary],
  )
  const util = totalPower > 0 ? Math.min(1, totalMach / totalPower) : 0
  const sortedProducts = useMemo(
    () => [...products].sort((a, b) => b.produced - a.produced || b.mach_sec - a.mach_sec),
    [products],
  )

  const allCols: {
    title: string
    dataIndex?: string
    render?: (v: unknown, r: SummaryItem) => ReactNode
    sorter?: (a: SummaryItem, b: SummaryItem) => number
  }[] = [
    { title: '机床', dataIndex: 'machine', sorter: (a, b) => a.machine.localeCompare(b.machine) },
    {
      title: '加工时长',
      render: (_v, r) => fmtDur(r.mach_sec),
      sorter: (a, b) => a.mach_sec - b.mach_sec,
    },
    {
      title: '开机时间',
      render: (_v, r) => fmtDur(r.power_sec),
      sorter: (a, b) => a.power_sec - b.power_sec,
    },
    {
      title: '利用率',
      render: (_v, r) => <Progress percent={Math.round(r.util_rate * 100)} size="small" />,
      sorter: (a, b) => a.util_rate - b.util_rate,
    },
    {
      title: '产量(件)',
      dataIndex: 'produced',
      sorter: (a, b) => a.produced - b.produced,
    },
    {
      title: '末次数据',
      render: (_v, r) => dayjs.unix(r.last.ts).format('MM-DD HH:mm:ss'),
      sorter: (a, b) => a.last.ts - b.last.ts,
    },
  ]

  const prodCols: {
    title: string
    dataIndex?: string
    render?: (v: unknown, r: ProductItem) => ReactNode
    sorter?: (a: ProductItem, b: ProductItem) => number
  }[] = [
    {
      title: '产品(程序注释)',
      render: (_v, r) => (r.comment && r.comment !== 'UNAVAILABLE' ? r.comment : <Tag>未知</Tag>),
    },
    {
      title: '产量(件)',
      dataIndex: 'produced',
      sorter: (a, b) => a.produced - b.produced,
    },
    {
      title: '加工时长',
      render: (_v, r) => fmtDur(r.mach_sec),
      sorter: (a, b) => a.mach_sec - b.mach_sec,
    },
    {
      title: '时长占比',
      render: (_v, r) => (
        <Progress
          percent={totalMach > 0 ? Math.round((r.mach_sec / totalMach) * 100) : 0}
          size="small"
        />
      ),
    },
    {
      title: '件数区间',
      render: (_v, r) => `${r.start_total} → ${r.end_total}`,
    },
    {
      title: '末次加工时间',
      render: (_v, r) => dayjs.unix(r.last_ts).format('MM-DD HH:mm'),
      sorter: (a, b) => a.last_ts - b.last_ts,
    },
  ]

  return (
    <div>
      <Card>
        <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap', alignItems: 'center' }}>
          <Select
            style={{ width: 170 }}
            placeholder="选择机床"
            value={machine}
            onChange={setMachine}
            options={[{ value: ALL_MACHINES, label: '全部机床' }, ...machines.map((m) => ({ value: m, label: m }))]}
          />
          <Segmented
            value={RANGES.find((r) => {
              const [a, b] = r.range()
              return a.unix() === range[0].unix() && b.unix() === range[1].unix()
            })?.key}
            onChange={(k) => {
              const found = RANGES.find((r) => r.key === k)
              if (found) setRange(found.range())
            }}
            options={RANGES.map((r) => ({ value: r.key, label: r.label }))}
          />
          <RangePicker
            showTime
            value={range}
            onChange={(v) => v && setRange([v[0]!, v[1]!])}
          />
          <Select
            style={{ width: 130 }}
            value={bucket}
            onChange={setBucket}
            options={[
              { value: 600, label: '10分钟' },
              { value: 1800, label: '30分钟' },
              { value: 3600, label: '1小时' },
              { value: 7200, label: '2小时' },
              { value: 14400, label: '4小时' },
            ]}
          />
        </div>
      </Card>

      <Row gutter={[16, 16]} style={{ marginTop: 16 }}>
        <Col xs={12} md={6}>
          <Card>
            <Statistic title="加工时长" value={fmtDur(totalMach)} />
          </Card>
        </Col>
        <Col xs={12} md={6}>
          <Card>
            <Statistic title="开机时间" value={fmtDur(totalPower)} />
          </Card>
        </Col>
        <Col xs={12} md={6}>
          <Card>
            <Statistic title="利用率(加工/开机)" value={pct(util)} />
          </Card>
        </Col>
        <Col xs={12} md={6}>
          <Card>
            <Statistic title="产量(件)" value={totalProd} />
          </Card>
        </Col>
      </Row>

      <Row gutter={[16, 16]} style={{ marginTop: 16 }}>
        <Col xs={24} lg={12}>
          <Card title="分时段加工时间" style={{ marginBottom: 16 }}>
            {buckets.length ? (
              <EChart option={machOption} height={340} />
            ) : (
              <Empty description="所选时段暂无数据" />
            )}
          </Card>
        </Col>
        <Col xs={24} lg={12}>
          <Card title="分时段产量" style={{ marginBottom: 16 }}>
            {buckets.length ? (
              <EChart option={prodOption} height={340} />
            ) : (
              <Empty description="所选时段暂无数据" />
            )}
          </Card>
        </Col>
      </Row>

      {machine === ALL_MACHINES ? (
        <Card title="各机床加工统计（所选时段）">
          <Table
            rowKey="machine"
            size="small"
            dataSource={summary}
            columns={allCols}
            pagination={false}
            locale={{ emptyText: <Empty description="所选时段暂无数据" /> }}
          />
        </Card>
      ) : (
        <Card title="分产品统计（产品名 = 程序注释）">
          <Table
            rowKey="comment"
            size="small"
            dataSource={sortedProducts}
            columns={prodCols}
            pagination={false}
            locale={{ emptyText: <Empty description="所选时段暂无数据" /> }}
          />
        </Card>
      )}
    </div>
  )
}
