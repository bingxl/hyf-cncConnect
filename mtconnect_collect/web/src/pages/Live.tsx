import { useEffect, useMemo, useState } from 'react'
import { Badge, Card, Col, Empty, Row, Segmented, Tag } from 'antd'
import dayjs from 'dayjs'
import { apiLive, type LiveItem } from '../api'
import { execColor, execText, modeText } from '../format'

const FILTERS = [
  { key: 'all', label: '全部' },
  { key: 'ACTIVE', label: '加工中' },
  { key: 'READY', label: '待机' },
  { key: 'STOPPED', label: '停止' },
  { key: 'INTERRUPTED', label: '暂停' },
  { key: 'UNAVAILABLE', label: '离线' },
] as const

type FilterKey = (typeof FILTERS)[number]['key']

const isOffline = (l: LiveItem) => !l.available || l.execution === 'UNAVAILABLE'

export default function Live() {
  const [items, setItems] = useState<LiveItem[]>([])
  const [ts, setTs] = useState(Date.now())
  const [filter, setFilter] = useState<FilterKey>('all')

  useEffect(() => {
    const load = () => {
      apiLive()
        .then((r) => {
          setItems(r.items)
          setTs(Date.now())
        })
        .catch(() => {})
    }
    load()
    const t = setInterval(load, 5000)
    return () => clearInterval(t)
  }, [])

  const online = items.filter((i) => i.available && i.execution !== 'UNAVAILABLE').length
  const machining = items.filter((i) => i.execution === 'ACTIVE').length

  const filtered = useMemo(() => {
    const list = items.filter((l) => {
      if (filter === 'all') return true
      if (filter === 'UNAVAILABLE') return isOffline(l)
      return l.execution === filter
    })
    return list.sort((a, b) => {
      const pa = a.execution === 'ACTIVE' ? 0 : isOffline(a) ? 2 : 1
      const pb = b.execution === 'ACTIVE' ? 0 : isOffline(b) ? 2 : 1
      return pa - pb || a.name.localeCompare(b.name)
    })
  }, [filter, items])

  const filterOptions = FILTERS.map((f) => {
    const count =
      f.key === 'all'
        ? items.length
        : f.key === 'UNAVAILABLE'
          ? items.filter(isOffline).length
          : items.filter((l) => l.execution === f.key).length
    return { value: f.key, label: `${f.label} ${count}` }
  })

  return (
    <div>
      <Card>
        <Segmented value={filter} onChange={(k) => setFilter(k as FilterKey)} options={filterOptions} />
      </Card>
      <Row gutter={[16, 16]} style={{ marginTop: 16 }}>
        {filtered.map((m) => (
          <Col xs={12} md={8} lg={6} key={m.name}>
            <Card
              size="small"
              title={
                <span>
                  <Badge status={m.execution === 'ACTIVE' ? 'processing' : isOffline(m) ? 'error' : 'default'} />
                  {m.name}
                </span>
              }
              extra={<Tag color={execColor(m.execution)}>{execText(m.execution)}</Tag>}
            >
              <p style={{ margin: 0 }}>
                模式：<b>{modeText(m.mode)}</b>
              </p>
              <p style={{ margin: '4px 0' }}>
                程序：<b>{m.program}</b>
              </p>
              <p style={{ margin: '4px 0' }}>
                产品：<b style={{ fontSize: 12 }}>{m.comment}</b>
              </p>
              <p style={{ margin: '4px 0' }}>
                累计件数：<b>{m.part_total ?? '-'}</b>
              </p>
            </Card>
          </Col>
        ))}
      </Row>
      {!filtered.length && (
        <div style={{ marginTop: 32 }}>
          <Empty description="当前筛选条件下无机床" />
        </div>
      )}
      <p style={{ marginTop: 12, color: '#888' }}>
        在线 {online} 台，加工中 {machining} 台 · 更新于 {dayjs(ts).format('HH:mm:ss')}（每 5 秒刷新）
      </p>
    </div>
  )
}
