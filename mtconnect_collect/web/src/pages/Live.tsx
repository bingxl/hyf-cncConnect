import { useEffect, useState } from 'react'
import { Badge, Card, Col, Row, Tag } from 'antd'
import dayjs from 'dayjs'
import { apiLive, type LiveItem } from '../api'

const execColor = (e: string) =>
  e === 'ACTIVE' ? 'green' : e === 'UNAVAILABLE' ? 'default' : e === 'READY' ? 'blue' : 'orange'
const execText = (e: string) =>
  e === 'ACTIVE' ? '加工中' : e === 'READY' ? '待机' : e === 'STOPPED' ? '停止' : e === 'INTERRUPTED' ? '暂停' : e

export default function Live() {
  const [items, setItems] = useState<LiveItem[]>([])
  const [ts, setTs] = useState(Date.now())

  useEffect(() => {
    const load = () => {
      apiLive().then((r) => {
        setItems(r.items)
        setTs(Date.now())
      }).catch(() => {})
    }
    load()
    const t = setInterval(load, 5000)
    return () => clearInterval(t)
  }, [])

  const online = items.filter((i) => i.available && i.execution !== 'UNAVAILABLE').length
  const machining = items.filter((i) => i.execution === 'ACTIVE').length

  return (
    <div>
      <Row gutter={[16, 16]}>
        {items.map((m) => (
          <Col xs={12} md={8} lg={6} key={m.name}>
            <Card
              size="small"
              title={
                <span>
                  <Badge status={m.execution === 'ACTIVE' ? 'processing' : 'default'} />
                  {m.name}
                </span>
              }
              extra={<Tag color={execColor(m.execution)}>{execText(m.execution)}</Tag>}
            >
              <p style={{ margin: 0 }}>
                模式：<b>{m.mode}</b>
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
      <p style={{ marginTop: 12, color: '#888' }}>
        在线 {online} 台，加工中 {machining} 台 · 更新于 {dayjs(ts).format('HH:mm:ss')}（每 5 秒刷新）
      </p>
    </div>
  )
}
