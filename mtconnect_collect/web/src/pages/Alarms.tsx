import { useEffect, useMemo, useState } from 'react'
import { Card, Empty, Select, Table, Tag } from 'antd'
import dayjs, { type Dayjs } from 'dayjs'
import { DatePicker } from 'antd'
import {
  apiAlarmsCurrent,
  apiAlarmsHistory,
  apiMachines,
  type AlarmItem,
} from '../api'
import { fmtDur } from '../format'

const { RangePicker } = DatePicker

const stateColor = (s: string): string => {
  switch (s) {
    case 'FAULT':
    case 'FAILED':
    case 'ESTOP':
      return 'red'
    case 'WARNING':
      return 'orange'
    default:
      return 'default'
  }
}

const stateText = (s: string): string => {
  switch (s) {
    case 'FAULT':
      return '故障'
    case 'FAILED':
      return '失败'
    case 'WARNING':
      return '警告'
    case 'ESTOP':
      return '急停'
    case 'NORMAL':
      return '正常'
    case 'UNAVAILABLE':
      return '不可用'
    default:
      return s || '-'
  }
}

const duration = (a: AlarmItem): string => {
  if (a.active) return `${fmtDur(Date.now() / 1000 - a.first_ts)}（持续中）`
  if (a.end_ts != null) return fmtDur(a.end_ts - a.first_ts)
  return '-'
}

export default function Alarms() {
  const [machines, setMachines] = useState<string[]>([])
  const [machine, setMachine] = useState<string>()
  const [range, setRange] = useState<[Dayjs, Dayjs]>([
    dayjs().subtract(24, 'hour'),
    dayjs(),
  ])
  const [current, setCurrent] = useState<AlarmItem[]>([])
  const [history, setHistory] = useState<AlarmItem[]>([])

  useEffect(() => {
    apiMachines()
      .then((r) => setMachines(r.machines.map((m) => m.name)))
      .catch(console.error)
  }, [])

  useEffect(() => {
    const load = () => {
      apiAlarmsCurrent()
        .then((r) => setCurrent(r.items))
        .catch(() => {})
    }
    load()
    const t = setInterval(load, 10000)
    return () => clearInterval(t)
  }, [])

  useEffect(() => {
    const from = range[0].unix()
    const to = range[1].unix()
    apiAlarmsHistory(from, to, machine)
      .then((r) => setHistory(r.items))
      .catch(console.error)
  }, [machine, range])

  const columns = useMemo(
    () => [
      { title: '机床', dataIndex: 'machine', width: 90 },
      { title: '报警项', dataIndex: 'item_id', ellipsis: true },
      { title: '类型', dataIndex: 'item_type', width: 110 },
      {
        title: '状态',
        dataIndex: 'state',
        width: 80,
        render: (s: string) => <Tag color={stateColor(s)}>{stateText(s)}</Tag>,
      },
      {
        title: '开始时间',
        dataIndex: 'first_ts',
        width: 140,
        render: (v: number) => dayjs.unix(v).format('MM-DD HH:mm:ss'),
      },
      {
        title: '最近时间',
        dataIndex: 'last_ts',
        width: 140,
        render: (v: number) => dayjs.unix(v).format('MM-DD HH:mm:ss'),
      },
      {
        title: '结束时间',
        dataIndex: 'end_ts',
        width: 140,
        render: (v: number | null) =>
          v == null ? <Tag color="red">持续中</Tag> : dayjs.unix(v).format('MM-DD HH:mm:ss'),
      },
      {
        title: '持续时长',
        width: 130,
        render: (_: unknown, r: AlarmItem) => duration(r),
      },
    ],
    [],
  )

  return (
    <div>
      <Card title="当前报警" style={{ marginBottom: 16 }}>
        <Table
          rowKey={(r) => `${r.machine}:${r.item_id}:${r.first_ts}`}
          size="small"
          dataSource={current}
          columns={columns}
          pagination={false}
          locale={{ emptyText: <Empty description="当前无报警" /> }}
        />
      </Card>

      <Card
        title="历史报警"
        extra={
          <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
            <Select
              style={{ width: 140 }}
              placeholder="全部机床"
              allowClear
              value={machine}
              onChange={setMachine}
              options={machines.map((m) => ({ value: m, label: m }))}
            />
            <RangePicker
              showTime
              value={range}
              onChange={(v) => v && setRange([v[0]!, v[1]!])}
            />
          </div>
        }
      >
        <Table
          rowKey={(r) => `${r.machine}:${r.item_id}:${r.first_ts}`}
          size="small"
          dataSource={history}
          columns={columns}
          pagination={{ pageSize: 20, showSizeChanger: false }}
          locale={{ emptyText: <Empty /> }}
        />
      </Card>
    </div>
  )
}
