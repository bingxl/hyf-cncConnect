import { useEffect, useMemo, useState } from 'react'
import type { ReactNode } from 'react'
import { Card, Col, Empty, Progress, Row, Statistic, Table, Tag } from 'antd'
import * as echarts from 'echarts'
import dayjs from 'dayjs'
import { apiShifts, type ShiftItem, type ShiftMachine, type ShiftProduct } from '../api'
import EChart from './EChart'
import { fmtDur, pct } from '../format'

const renderProducts = (products: ShiftProduct[]): ReactNode =>
  products.length ? (
    <div>
      {products.map((p) => (
        <Tag key={p.comment} style={{ marginBottom: 4 }}>
          {p.comment} ×{p.produced}
        </Tag>
      ))}
    </div>
  ) : (
    <span style={{ color: '#999' }}>-</span>
  )

export default function ShiftView({
  from,
  to,
  machine,
}: {
  from: number
  to: number
  machine: string
}) {
  const [shifts, setShifts] = useState<ShiftItem[]>([])
  const [loading, setLoading] = useState(false)

  useEffect(() => {
    setLoading(true)
    apiShifts(from, to, machine)
      .then((r) => setShifts(r.shifts))
      .catch(console.error)
      .finally(() => setLoading(false))
  }, [from, to, machine])

  const labels = useMemo(
    () => shifts.map((w) => `${w.date.slice(5)} ${w.shift === 'day' ? '白' : '夜'}`),
    [shifts],
  )
  const machHours = useMemo(
    () => shifts.map((w) => +(w.fleet.mach_sec / 3600).toFixed(2)),
    [shifts],
  )
  const produced = useMemo(() => shifts.map((w) => w.fleet.produced), [shifts])
  const utilPct = useMemo(
    () => shifts.map((w) => +(w.fleet.util_rate * 100).toFixed(1)),
    [shifts],
  )
  /* 分产品堆叠：取总件数前 8 的产品作为图例 */
  const productNames = useMemo(() => {
    const total = new Map<string, number>()
    for (const w of shifts)
      for (const p of w.fleet.products) total.set(p.comment, (total.get(p.comment) ?? 0) + p.produced)
    return [...total.entries()]
      .sort((a, b) => b[1] - a[1])
      .slice(0, 8)
      .map(([c]) => c)
  }, [shifts])
  const productSeries = useMemo(
    () =>
      productNames.map((c) => ({
        name: c,
        type: 'bar' as const,
        stack: 'prod',
        data: shifts.map((w) => w.fleet.products.find((p) => p.comment === c)?.produced ?? 0),
      })),
    [productNames, shifts],
  )

  const machProdOption: echarts.EChartsOption = useMemo(
    () => ({
      tooltip: { trigger: 'axis' },
      legend: { data: ['加工时长', '产量'], top: 0 },
      grid: { left: 8, right: 8, top: 64, bottom: 8, containLabel: true },
      xAxis: { type: 'category', data: labels, axisLabel: { rotate: 30 } },
      yAxis: [
        { type: 'value', name: '加工时长(h)' },
        { type: 'value', name: '产量(件)', splitLine: { show: false } },
      ],
      series: [
        {
          name: '加工时长',
          type: 'bar',
          data: machHours,
          itemStyle: { color: '#1677ff' },
          barMaxWidth: 42,
        },
        {
          name: '产量',
          type: 'line',
          yAxisIndex: 1,
          data: produced,
          smooth: true,
          itemStyle: { color: '#fa8c16' },
        },
      ],
    }),
    [labels, machHours, produced],
  )

  const utilOption: echarts.EChartsOption = useMemo(
    () => ({
      tooltip: { trigger: 'axis' },
      grid: { left: 8, right: 8, top: 48, bottom: 8, containLabel: true },
      xAxis: { type: 'category', data: labels, axisLabel: { rotate: 30 } },
      yAxis: { type: 'value', name: '利用率(%)', max: 100 },
      series: [
        {
          name: '利用率',
          type: 'bar',
          data: utilPct,
          itemStyle: { color: '#52c41a' },
          barMaxWidth: 42,
          label: { show: true, position: 'top', formatter: '{c}%' },
        },
      ],
    }),
    [labels, utilPct],
  )

  const productOption: echarts.EChartsOption = useMemo(
    () => ({
      tooltip: { trigger: 'axis' },
      legend: { type: 'scroll', top: 0 },
      grid: { left: 8, right: 8, top: 56, bottom: 8, containLabel: true },
      xAxis: { type: 'category', data: labels, axisLabel: { rotate: 30 } },
      yAxis: { type: 'value', name: '件' },
      series: productSeries,
    }),
    [labels, productSeries],
  )

  const columns: {
    title: string
    dataIndex?: string
    render?: (v: unknown, r: ShiftMachine) => ReactNode
  }[] = [
    { title: '机台', dataIndex: 'machine' },
    {
      title: '加工时长',
      render: (_v, r) => fmtDur(r.mach_sec),
    },
    {
      title: '开机时长',
      render: (_v, r) => fmtDur(r.power_sec),
    },
    {
      title: '利用率',
      render: (_v, r) => <Progress percent={Math.round(r.util_rate * 100)} size="small" />,
    },
    {
      title: '产量(件)',
      dataIndex: 'produced',
    },
    {
      title: '产品(件数)',
      render: (_v, r) => renderProducts(r.products),
    },
  ]

  return (
    <div>
      {!shifts.length && !loading && (
        <div style={{ marginTop: 16 }}>
          <Empty description="所选日期暂无数据" />
        </div>
      )}

      {shifts.length > 0 && (
        <>
          <Row gutter={[16, 16]} style={{ marginTop: 16 }}>
            <Col xs={24} lg={14}>
              <Card title="各班次加工时长与产量">
                <EChart option={machProdOption} height={320} />
              </Card>
            </Col>
            <Col xs={24} lg={10}>
              <Card title="各班次利用率">
                <EChart option={utilOption} height={320} />
              </Card>
            </Col>
          </Row>
          <Card title="各班次分产品件数（堆叠）" style={{ marginTop: 16 }}>
            <EChart option={productOption} height={320} />
          </Card>
        </>
      )}

      {shifts.map((w) => {
        const partial = w.ws > w.start || w.we < w.end
        return (
          <Card
            key={`${w.shift}-${w.date}`}
            style={{ marginTop: 16 }}
            title={
              <span>
                <Tag color={w.shift === 'day' ? 'blue' : 'purple'}>
                  {w.shift === 'day' ? '白班' : '夜班'}
                </Tag>
                {w.date} · {w.label}
                {partial && (
                  <Tag style={{ marginLeft: 8 }} color="orange">
                    部分时段
                  </Tag>
                )}
              </span>
            }
            extra={
              <span style={{ color: '#888', fontSize: 12 }}>
                {dayjs.unix(w.ws).format('MM-DD HH:mm')} ~ {dayjs.unix(w.we).format('HH:mm')}
              </span>
            }
          >
            <Row gutter={[16, 16]}>
              <Col xs={12} md={6}>
                <Statistic title="总加工时长" value={fmtDur(w.fleet.mach_sec)} />
              </Col>
              <Col xs={12} md={6}>
                <Statistic title="总开机时长" value={fmtDur(w.fleet.power_sec)} />
              </Col>
              <Col xs={12} md={6}>
                <Statistic title="利用率" value={pct(w.fleet.util_rate)} />
              </Col>
              <Col xs={12} md={6}>
                <Statistic title="总产量(件)" value={w.fleet.produced} />
              </Col>
            </Row>

            <Table
              style={{ marginTop: 16 }}
              rowKey="machine"
              size="small"
              loading={loading}
              dataSource={w.machines}
              columns={columns}
              pagination={false}
              locale={{ emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="该班次无采样" /> }}
            />

            {w.fleet.products.length > 0 && (
              <div style={{ marginTop: 12 }}>
                <div style={{ marginBottom: 8, fontWeight: 600 }}>该班次产品汇总（件数）</div>
                <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8 }}>
                  {w.fleet.products.map((p) => (
                    <Tag key={p.comment} color="geekblue">
                      {p.comment} ×{p.produced}
                    </Tag>
                  ))}
                </div>
              </div>
            )}
          </Card>
        )
      })}
    </div>
  )
}
