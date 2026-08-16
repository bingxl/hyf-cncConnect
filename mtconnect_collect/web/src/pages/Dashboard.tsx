import { useCallback, useEffect, useMemo, useState } from 'react'
import type { ReactNode } from 'react'
import { Button, Card, Col, Empty, Progress, Row, Statistic, Table, Tag } from 'antd'
import {
  ClockCircleOutlined,
  FireOutlined,
  LineChartOutlined,
  ReloadOutlined,
  ThunderboltOutlined,
} from '@ant-design/icons'
import * as echarts from 'echarts'
import dayjs from 'dayjs'
import { apiLive, apiSummary, type LiveItem, type SummaryItem } from '../api'
import EChart from '../components/EChart'
import { execColor, execText, fmtDur, toHours } from '../format'

const RANGE_HOURS = 24

export default function Dashboard() {
  const [summary, setSummary] = useState<SummaryItem[]>([])
  const [live, setLive] = useState<LiveItem[]>([])
  const [loading, setLoading] = useState(true)
  const [nowTs, setNowTs] = useState(() => Math.floor(Date.now() / 1000))

  const load = useCallback(() => {
    const from = nowTs - RANGE_HOURS * 3600
    apiSummary(from, nowTs)
      .then((r) => setSummary(r.items))
      .catch(console.error)
    apiLive()
      .then((r) => setLive(r.items))
      .catch(() => {})
  }, [nowTs])

  useEffect(() => {
    setLoading(true)
    load()
    setLoading(false)
    const t = setInterval(load, 30000)
    return () => clearInterval(t)
  }, [load])

  const machines = useMemo(() => summary.map((s) => s.machine), [summary])
  const totalMach = useMemo(() => summary.reduce((a, b) => a + b.mach_sec, 0), [summary])
  const totalPower = useMemo(() => summary.reduce((a, b) => a + b.power_sec, 0), [summary])
  const totalProduced = useMemo(() => summary.reduce((a, b) => a + b.produced, 0), [summary])
  const online = useMemo(
    () => live.filter((l) => l.available && l.execution !== 'UNAVAILABLE').length,
    [live],
  )
  const machiningNow = useMemo(() => live.filter((l) => l.execution === 'ACTIVE').length, [live])
  const avgUtil = useMemo(
    () => (machines.length ? summary.reduce((a, b) => a + b.util_rate, 0) / machines.length : 0),
    [machines.length, summary],
  )

  const barOption: echarts.EChartsOption = useMemo(
    () => ({
      tooltip: { trigger: 'axis' },
      legend: { data: ['加工时长', '产量'] },
      grid: { left: 60, right: 60, top: 40, bottom: 70 },
      xAxis: { type: 'category', data: machines, axisLabel: { interval: 0, rotate: 30 } },
      yAxis: [
        { type: 'value', name: '加工时长(h)', minInterval: 1 },
        { type: 'value', name: '产量(件)', splitLine: { show: false }, minInterval: 1 },
      ],
      series: [
        {
          name: '加工时长',
          type: 'bar',
          data: summary.map((s) => toHours(s.mach_sec)),
          itemStyle: { color: '#1677ff' },
          barMaxWidth: 30,
        },
        {
          name: '产量',
          type: 'line',
          yAxisIndex: 1,
          data: summary.map((s) => s.produced),
          smooth: true,
          itemStyle: { color: '#fa8c16' },
        },
      ],
    }),
    [machines, summary],
  )

  const columns: {
    title: string
    dataIndex?: string
    render?: (v: unknown, r: SummaryItem) => ReactNode
    sorter?: (a: SummaryItem, b: SummaryItem) => number
  }[] = [
    {
      title: '机床',
      dataIndex: 'machine',
      sorter: (a, b) => a.machine.localeCompare(b.machine),
    },
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
      title: '时产量(件/h)',
      render: (_v, r) => (r.mach_sec > 0 ? (r.produced / (r.mach_sec / 3600)).toFixed(1) : '-'),
    },
    {
      title: '当前状态',
      render: (_v, r) => {
        const l = live.find((x) => x.name === r.machine)
        return l ? <Tag color={execColor(l.execution)}>{execText(l.execution)}</Tag> : <Tag>离线</Tag>
      },
    },
    {
      title: '当前产品',
      render: (_v, r) => r.last.comment || '-',
    },
    {
      title: '末次数据',
      render: (_v, r) => dayjs.unix(r.last.ts).format('MM-DD HH:mm:ss'),
      sorter: (a, b) => a.last.ts - b.last.ts,
    },
  ]

  return (
    <div>
      <Row gutter={[16, 16]}>
        <Col xs={12} md={6}>
          <Card>
            <Statistic title="机床总数" value={machines.length} />
          </Card>
        </Col>
        <Col xs={12} md={6}>
          <Card>
            <Statistic
              title={`${RANGE_HOURS}h 总加工时长`}
              value={totalMach}
              formatter={(v) => fmtDur(Number(v))}
              prefix={<ClockCircleOutlined />}
            />
            <div style={{ color: '#888', fontSize: 12, marginTop: 4 }}>
              开机时长 {fmtDur(totalPower)} · 平均利用率 {Math.round(avgUtil * 100)}%
            </div>
          </Card>
        </Col>
        <Col xs={12} md={6}>
          <Card>
            <Statistic title={`${RANGE_HOURS}h 总产量(件)`} value={totalProduced} prefix={<FireOutlined />} />
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
          <Card
            title={`${RANGE_HOURS}h 各机床加工时长与产量`}
            extra={
              <Button size="small" icon={<ReloadOutlined />} onClick={() => setNowTs(Math.floor(Date.now() / 1000))}>
                刷新
              </Button>
            }
          >
            {summary.length ? (
              <EChart option={barOption} height={340} />
            ) : (
              <Empty description="所选时段暂无采样数据，请确认 mtc_stats 采集进程与 agent 已启动" />
            )}
          </Card>
        </Col>
        <Col xs={24} lg={8}>
          <Card title="实时运行状态" extra={<LineChartOutlined />}>
            <div style={{ marginBottom: 8 }}>
              平均利用率 <b>{Math.round(avgUtil * 100)}%</b>
              <span style={{ color: '#888', marginLeft: 8 }}>
                {dayjs.unix(nowTs - RANGE_HOURS * 3600).format('MM-DD HH:mm')} ~{' '}
                {dayjs.unix(nowTs).format('HH:mm')}
              </span>
            </div>
            <div style={{ maxHeight: 330, overflowY: 'auto' }}>
              {live.length ? (
                live.map((l) => (
                  <div
                    key={l.name}
                    style={{ display: 'flex', justifyContent: 'space-between', padding: '4px 0' }}
                  >
                    <span>{l.name}</span>
                    <Tag color={execColor(l.execution)}>{execText(l.execution)}</Tag>
                  </div>
                ))
              ) : (
                <Empty description="暂无在线数据" image={Empty.PRESENTED_IMAGE_SIMPLE} />
              )}
            </div>
          </Card>
        </Col>
      </Row>

      <Card title={`${RANGE_HOURS}h 机床汇总表`} style={{ marginTop: 16 }}>
        <Table
          rowKey="machine"
          size="small"
          loading={loading}
          dataSource={summary}
          columns={columns}
          pagination={false}
        />
      </Card>
    </div>
  )
}
