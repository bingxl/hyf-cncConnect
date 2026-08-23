import { useState } from 'react'
import { Layout, Menu, theme } from 'antd'
import {
  DashboardOutlined,
  BarChartOutlined,
  MonitorOutlined,
  WarningOutlined,
} from '@ant-design/icons'
import Dashboard from './pages/Dashboard'
import Stats from './pages/Stats'
import Live from './pages/Live'
import Alarms from './pages/Alarms'

const { Header, Content } = Layout

type PageKey = 'dashboard' | 'stats' | 'live' | 'alarms'

export default function App() {
  const [page, setPage] = useState<PageKey>('dashboard')
  const {
    token: { colorBgContainer, borderRadiusLG },
  } = theme.useToken()

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <Header style={{ display: 'flex', alignItems: 'center' }}>
        <div style={{ color: '#fff', fontSize: 18, fontWeight: 600, marginRight: 32 }}>
          机床数据浏览
        </div>
        <Menu
          theme="dark"
          mode="horizontal"
          selectedKeys={[page]}
          onClick={(e) => setPage(e.key as PageKey)}
          items={[
            { key: 'dashboard', icon: <DashboardOutlined />, label: '总览' },
            { key: 'stats', icon: <BarChartOutlined />, label: '加工统计' },
            { key: 'live', icon: <MonitorOutlined />, label: '实时状态' },
            { key: 'alarms', icon: <WarningOutlined />, label: '报警' },
          ]}
          style={{ flex: 1 }}
        />
      </Header>
      <Content style={{ padding: 24 }}>
        <div
          style={{
            background: colorBgContainer,
            borderRadius: borderRadiusLG,
            padding: 24,
            minHeight: 360,
          }}
        >
          {page === 'dashboard' && <Dashboard />}
          {page === 'stats' && <Stats />}
          {page === 'live' && <Live />}
          {page === 'alarms' && <Alarms />}
        </div>
      </Content>
    </Layout>
  )
}
