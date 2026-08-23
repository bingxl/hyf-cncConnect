import { useEffect, useMemo, useState } from 'react'
import { Card, DatePicker, Segmented, Select } from 'antd'
import dayjs, { type Dayjs } from 'dayjs'
import { ALL_MACHINES, apiMachines } from '../api'
import ShiftView from '../components/ShiftView'

const { RangePicker } = DatePicker

const RANGES = [
  { key: 'today', label: '今日', range: (): [Dayjs, Dayjs] => [dayjs(), dayjs()] },
  {
    key: 'yesterday',
    label: '昨日',
    range: (): [Dayjs, Dayjs] => [dayjs().subtract(1, 'day'), dayjs().subtract(1, 'day')],
  },
  { key: '7d', label: '近7天', range: (): [Dayjs, Dayjs] => [dayjs().subtract(6, 'day'), dayjs()] },
  { key: 'month', label: '本月', range: (): [Dayjs, Dayjs] => [dayjs().startOf('month'), dayjs()] },
]

export default function Stats() {
  const [machines, setMachines] = useState<string[]>([])
  const [machine, setMachine] = useState<string>(ALL_MACHINES)
  const [range, setRange] = useState<[Dayjs, Dayjs]>([dayjs(), dayjs()])
  /* 只按日期选择：from=开始日 00:00；
     夜班 20:30 跨到次日 08:30，to 需延伸到次日 08:30，否则夜班被截断在 24:00
     （后端按 ws<we 严格判交，白班 08:30 边界不会误入） */
  const from = useMemo(() => range[0].startOf('day').unix(), [range])
  const to = useMemo(
    () => range[1].startOf('day').add(1, 'day').add(8, 'hour').add(30, 'minute').unix(),
    [range],
  )

  useEffect(() => {
    apiMachines()
      .then((r) => setMachines(r.machines.map((m) => m.name)))
      .catch(console.error)
  }, [])

  return (
    <div>
      <Card>
        <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap', alignItems: 'center' }}>
          <Select
            style={{ width: 170 }}
            value={machine}
            onChange={setMachine}
            options={[
              { value: ALL_MACHINES, label: '全部机台' },
              ...machines.map((m) => ({ value: m, label: m })),
            ]}
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
          <RangePicker value={range} onChange={(v) => v && setRange([v[0]!, v[1]!])} />
          <span style={{ color: '#888', fontSize: 12 }}>
            班次：周一~周六 白班 08:30-20:30 / 夜班 20:30-08:30；周日 白班 08:30-17:00、无夜班
          </span>
        </div>
      </Card>
      <ShiftView from={from} to={to} machine={machine} />
    </div>
  )
}
