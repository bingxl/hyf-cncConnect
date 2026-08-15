import { useEffect, useMemo, useState } from 'react'
import { Card, Col, Row, Statistic, Table, Tag, Typography } from 'antd'
import {
  ClockCircleOutlined,
  FireOutlined,
  LineChartOutlined,
  ThunderboltOutlined,
} from '@ant-design/icons'
import * as echarts from 'echarts'
import { apiLive, apiSummary, type LiveItem, type SummaryItem } from '../api'
import EChart from '../components/EChart'

const fmt = (sec: number) => {
  const h = Math.floor(sec / 3600)
  const m = Math.floor((sec % 3600) / 60)
  return h > 0 ? `${h}h${m}m` : `${m}m`
}

const execColor = (e: string) =>
  e === 'ACTIVE' ? 'green' : e === 'UNAVAILABLE' ? 'default' : e === 'READY' ? 'blue' : 'orange'

export default function Dashboard() {
  const now = Math.floor(Date.now() / 1000)
  const [summary, setSummary] = useState<SummaryItem[]>([])
  const [live, setLive] = useState<LiveItem[]>([])

  useEffect(() => {
    const from = now - 24 * 3600
    apiSummary(from, now).then((r) => setSummary(r.items)).catch(console.error)
    apiLive().then((r) => setLive(r.items)).catch(console.error)
    const t = setInterval(() => {
      apiLive().then((r) => setLive(r.items)).catch(() => {})
    }, 10000)
    return () => clearInterval(t)
  }, [])

  const machines = useMemo(() => summary.map((s) => s.machine), [summary])
  const totalMach = useMemo(() => summary.reduce((a, b) => a + b.mach_sec, 0), [summary])
  const totalProduced = useMemo(() => summary.reduce((a, b) => a + b.produced, 0), [summary])
  const online = useMemo(
    () => live.filter((l) => l.available && l.execution !== 'UNAVAILABLE').length,
    [live],
  )
  const machiningNow = useMemo(() => live.filter((l) => l.execution === 'ACTIVE').length, [live])
  const avgUtil = useMemo(
    () => (machines.length ? summary.reduce((a, b) => a + b.util_rate, 0) / machines.length : 0),
    [summary, machines.length],
  )

  const barOption: echarts.EChartsOption = useMemo(
    () => ({
      tooltip: { trigger: 'axis' },
      grid: { left: 50, right: 20, top: 30, bottom: 40 },
      xAxis: { type: 'category', data: machines, axisLabel: { interval: 0, rotate: 30 } },
      yAxis: [{ type: 'value', name: '小时' }],
      series: [
        {
          name: '加工时长(h)',
          type: 'bar',
          data: summary.map((s) => +(s.mach_sec / 3600).toFixed(1)),
          itemStyle: { color: '#1677ff' },
        },
        {
          name: '产量(件)',
          type: 'line',
          yAxisIndex: 0,
          data: summary.map((s) => s.produced),
          itemStyle: { color: '#fa8c16' },
        },
      ],
    }),
    [machines, summary],
  )

  const columns = [
    { title: '机床', dataIndex: 'machine' },
    {
      title: '加工时长',
      render: (_: unknown, r: SummaryItem) => fmt(r.mach_sec),
    },
    {
      title: '利用率',
      render: (_: unknown, r: SummaryItem) => `${(r.util_rate * 100).toFixed(0)}%`,
    },
    { title: '产量(件)', dataIndex: 'produced' },
    {
      title: '当前状态',
      render: (_: unknown, r: SummaryItem) => {
        const l = live.find((x) => x.name === r.machine)
        if (!l) return <Tag>离线</Tag>
        return <Tag color={execColor(l.execution)}>{l.execution || 'N/A'}</Tag>
      },
    },
    {
      title: '当前产品',
      render: (_: unknown, r: SummaryItem) => r.last.comment || '-',
    },
  ]

  return (
    <div>
      <Row gutter={[16, 16]}>
        <Col xs={12} md={6}>
          <Card><Statistic title="机床总数" value={machines.length} /></Card>
        </Col>
        <Col xs={12} md={6}>
          <Card>
            <Statistic
              title="24h 总加工时长"
              value={totalMach}
              formatter={(v) => fmt(Number(v))}
              prefix={<ClockCircleOutlined />}
            />
          </Card>
        </Col>
        <Col xs={12} md={6}>
          <Card>
            <Statistic title="24h 总产量(件)" value={totalProduced} prefix={<FireOutlined />} />
          </Card>
        </Col>
        <Col xs={12} md={6}>
          <Card>
            <Statistic
              title="在线 / 加工中"
              value={`${online} / ${machiningNow}`}
              prefix={<ThunderboltOutlined />}
            />
          </Card>
        </Col>
      </Row>

      <Row gutter={[16, 16]} style={{ marginTop: 16 }}>
        <Col xs={24} lg={16}>
          <Card title="24h 各机床加工时长与产量" extra={<LineChartOutlined />}>
            <EChart option={barOption} height={340} />
          </Card>
        </Col>
        <Col xs={24} lg={8}>
          <Card title="实时运行状态">
            <Typography.Paragraph style={{ marginBottom: 8 }}>
              平均利用率 <b>{(avgUtil * 100).toFixed(0)}%</b>
            </Typography.Paragraph>
            {live.map((l) => (
              <div key={l.name} style={{ display: 'flex', justifyContent: 'space-between', padding: '4px 0' }}>
                <span>{l.name}</span>
                <Tag color={execColor(l.execution)}>{l.execution}</Tag>
              </div>
            ))}
          </Card>
        </Col>
      </Row>

      <Card title="24h 机床汇总表" style={{ marginTop: 16 }}>
        <Table
          rowKey="machine"
          size="small"
          dataSource={summary}
          columns={columns}
          pagination={false}
        />
      </Card>
    </div>
  )
}
