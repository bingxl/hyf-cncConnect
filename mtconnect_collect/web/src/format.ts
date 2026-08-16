import dayjs from 'dayjs'

// 时长展示：秒 -> "X小时Y分" / "Y分Z秒" / "Z秒"
export const fmtDur = (sec: number): string => {
  const s = Math.max(0, Math.round(sec))
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  const r = s % 60
  if (h > 0) return m > 0 ? `${h}小时${m}分` : `${h}小时`
  if (m > 0) return r > 0 ? `${m}分${r}秒` : `${m}分钟`
  return `${r}秒`
}

// 秒 -> 小时（保留两位小数），用于图表数值轴
export const toHours = (sec: number): number => +(sec / 3600).toFixed(2)

export const execText = (e: string): string => {
  switch (e) {
    case 'ACTIVE':
      return '加工中'
    case 'READY':
      return '待机'
    case 'STOPPED':
      return '停止'
    case 'INTERRUPTED':
      return '暂停'
    case 'UNAVAILABLE':
      return '离线'
    default:
      return e || '未知'
  }
}

export const execColor = (e: string): string => {
  switch (e) {
    case 'ACTIVE':
      return 'green'
    case 'READY':
      return 'blue'
    case 'STOPPED':
      return 'default'
    case 'INTERRUPTED':
      return 'orange'
    default:
      return 'default'
  }
}

export const modeText = (m: string): string => {
  switch (m) {
    case 'AUTOMATIC':
      return '自动'
    case 'MANUAL':
      return '手动'
    case 'MANUAL_DATA_INPUT':
      return 'MDI'
    default:
      return m || '-'
  }
}

export const pct = (v: number): string => `${Math.round(v * 100)}%`

// 分桶起点：今天内只显示 HH:mm，跨天显示 MM-DD HH:mm
export const bucketTime = (ts: number): string => {
  const d = dayjs.unix(ts)
  return d.isSame(dayjs(), 'day') ? d.format('HH:mm') : d.format('MM-DD HH:mm')
}
