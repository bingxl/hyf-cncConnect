import { useEffect, useMemo, useState } from 'react'
import { Card, Col, DatePicker, Empty, Row, Select, Statistic, Table, Tag } from 'antd'
import dayjs, { type Dayjs } from 'dayjs'
import * as echarts from 'echarts'
import {
  apiMachines,
  apiMachining,
  apiProduction,
  apiProducts,
  type ProductItem,
  type TimePoint,
} from '../api'
import EChart from '../components/EChart'

const { RangePicker } = DatePicker
const fmt = (sec: number) => {
  const h = Math.floor(sec / 3600)
  const m = Math.floor((sec % 3600) / 60)
  return h > 0 ? `${h}h${m}m` : `${m}m`
}

export default function Stats() {
  const [machines, setMachines] = useState<string[]>([])
  const [machine, setMachine] = useState<string>()
  const [range, setRange] = useState<[Dayjs, Dayjs]>([
    dayjs().subtract(24, 'hour'),
    dayjs(),
  ])
  const [machining, setMachining] = useState<TimePoint[]>([])
  const [production, setProduction] = useState<TimePoint[]>([])
  const [products, setProducts] = useState<ProductItem[]>([])
  const [bucket, setBucket] = useState(1800)

  useEffect(() => {
    apiMachines().then((r) => {
      const list = r.machines.map((m) => m.name)
      setMachines(list)
      if (!machine && list.length) setMachine(list[0])
    }).catch(console.error)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  useEffect(() => {
    if (!machine) return
    const from = range[0].unix()
    const to = range[1].unix()
    apiMachining(from, to, machine, bucket).then((r) => setMachining(r.points)).catch(console.error)
    apiProduction(from, to, machine, bucket).then((r) => setProduction(r.points)).catch(console.error)
    apiProducts(from, to, machine).then((r) => setProducts(r.items)).catch(console.error)
  }, [machine, range, bucket])

  const labels = useMemo(
    () => machining.map((p) => dayjs.unix(p.bucket_ts).format('MM-DD HH:mm')),
    [machining],
  )

  const machOption: echarts.EChartsOption = useMemo(
    () => ({
      tooltip: { trigger: 'axis' },
      grid: { left: 60, right: 30, top: 30, bottom: 60 },
      xAxis: { type: 'category', data: labels, axisLabel: { rotate: 45 } },
      yAxis: { type: 'value', name: '秒' },
      series: [
        {
          name: '加工时间',
          type: 'bar',
          data: machining.map((p) => p.mach_sec),
          itemStyle: { color: '#1677ff' },
        },
      ],
    }),
    [labels, machining],
  )

  const prodOption: echarts.EChartsOption = useMemo(
    () => ({
      tooltip: { trigger: 'axis' },
      grid: { left: 50, right: 30, top: 30, bottom: 60 },
      xAxis: { type: 'category', data: labels, axisLabel: { rotate: 45 } },
      yAxis: { type: 'value', name: '件' },
      series: [
        {
          name: '产量',
          type: 'line',
          data: production.map((p) => p.produced ?? 0),
          smooth: true,
          areaStyle: { opacity: 0.15 },
        },
      ],
    }),
    [labels, production],
  )

  const totalMach = machining.reduce((a, b) => a + b.mach_sec, 0)
  const totalProd = production.reduce((a, b) => a + (b.produced ?? 0), 0)

  const productCols = [
    {
      title: '产品(程序注释)',
      dataIndex: 'comment',
      render: (c: string) => (c && c !== 'UNAVAILABLE' ? c : <Tag>未知</Tag>),
    },
    { title: '产量(件)', dataIndex: 'produced' },
    {
      title: '加工时长',
      render: (_: unknown, r: ProductItem) => fmt(r.mach_sec),
    },
    {
      title: '累计件数区间',
      render: (_: unknown, r: ProductItem) => `${r.start_total} → ${r.end_total}`,
    },
    {
      title: '末次加工时间',
      render: (_: unknown, r: ProductItem) => dayjs.unix(r.last_ts).format('MM-DD HH:mm'),
    },
  ]

  return (
    <div>
      <Card>
        <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap', alignItems: 'center' }}>
          <Select
            style={{ width: 160 }}
            placeholder="选择机床"
            value={machine}
            onChange={setMachine}
            options={machines.map((m) => ({ value: m, label: m }))}
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
          <Card><Statistic title="加工时长" value={fmt(totalMach)} /></Card>
        </Col>
        <Col xs={12} md={6}>
          <Card><Statistic title="产量(件)" value={totalProd} /></Card>
        </Col>
        <Col xs={12} md={6}>
          <Card><Statistic title="产品数" value={products.filter((p) => p.produced > 0).length} /></Card>
        </Col>
        <Col xs={12} md={6}>
          <Card><Statistic title="采样点数" value={machining.reduce((a, b) => a + b.sample_count, 0)} /></Card>
        </Col>
      </Row>

      <Row gutter={[16, 16]} style={{ marginTop: 16 }}>
        <Col xs={24} lg={12}>
          <Card title="分时段加工时间" style={{ marginBottom: 16 }}>
            <EChart option={machOption} height={340} />
          </Card>
        </Col>
        <Col xs={24} lg={12}>
          <Card title="分时段产量" style={{ marginBottom: 16 }}>
            <EChart option={prodOption} height={340} />
          </Card>
        </Col>
      </Row>

      <Card title="分产品产量（产品名 = 程序注释）">
        <Table
          rowKey="comment"
          size="small"
          dataSource={products}
          columns={productCols}
          pagination={false}
          locale={{ emptyText: <Empty /> }}
        />
      </Card>
    </div>
  )
}

