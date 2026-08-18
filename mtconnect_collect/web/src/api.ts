// API client - talks to webserver (dev: vite proxy /api -> :8088)
const BASE = '/api'

// 加工统计页“全部机床”标记：后端按 machine=ALL 返回全厂汇总
export const ALL_MACHINES = 'ALL'

export interface MachineInfo {
  name: string
  first_ts: number
  last_ts: number
}

export interface SummaryItem {
  machine: string
  mach_sec: number
  power_sec: number
  util_rate: number
  part_total_start: number
  part_total_end: number
  produced: number
  sample_count: number
  machining_count: number
  last: {
    execution: string
    mode: string
    comment: string
    ts: number
  }
}

export interface TimePoint {
  bucket_ts: number
  mach_sec: number
  power_sec?: number
  machining_count: number
  sample_count: number
  produced?: number
  start_total?: number
  end_total?: number
}

export interface ProductItem {
  comment: string
  produced: number
  mach_sec: number
  start_total: number
  end_total: number
  first_ts: number
  last_ts: number
}

export interface LiveItem {
  name: string
  available: boolean
  execution: string
  mode: string
  tmmode: string
  program: string
  comment: string
  part_total: number | null
}

export interface AlarmItem {
  machine: string
  item_id: string
  item_type: string
  state: string
  first_ts: number
  last_ts: number
  end_ts: number | null
  active: number
}

export interface ShiftProduct {
  comment: string
  produced: number
}

export interface ShiftMachine {
  machine: string
  mach_sec: number
  power_sec: number
  util_rate: number
  produced: number
  products: ShiftProduct[]
}

export interface ShiftItem {
  shift: 'day' | 'night'
  date: string
  date_ts: number
  start: number
  end: number
  ws: number
  we: number
  label: string
  machines: ShiftMachine[]
  fleet: ShiftMachine
}

async function get<T>(path: string): Promise<T> {
  const res = await fetch(BASE + path)
  if (!res.ok) {
    const body = await res.text()
    throw new Error(body || `HTTP ${res.status}`)
  }
  return res.json() as Promise<T>
}

export function apiHealth() {
  return get<{ status: string; db_rows: number; first_sample: number; last_sample: number }>('/health')
}
export function apiMachines() {
  return get<{ machines: MachineInfo[] }>('/machines')
}
export function apiSummary(from: number, to: number) {
  return get<{ items: SummaryItem[] }>(`/stats/summary?from=${from}&to=${to}`)
}
export function apiMachining(from: number, to: number, machine: string, bucket = 1800) {
  return get<{ points: TimePoint[] }>(`/stats/machining?from=${from}&to=${to}&machine=${machine}&bucket=${bucket}`)
}
export function apiProduction(from: number, to: number, machine: string, bucket = 1800) {
  return get<{ points: TimePoint[] }>(`/stats/production?from=${from}&to=${to}&machine=${machine}&bucket=${bucket}`)
}
export function apiProducts(from: number, to: number, machine: string) {
  return get<{ items: ProductItem[] }>(`/stats/products?from=${from}&to=${to}&machine=${machine}`)
}
export function apiLive() {
  return get<{ items: LiveItem[] }>('/live/current')
}
export function apiAlarmsCurrent() {
  return get<{ items: AlarmItem[] }>('/alarms/current')
}
export function apiAlarmsHistory(from: number, to: number, machine?: string) {
  const m =
    machine && machine !== ALL_MACHINES
      ? `&machine=${encodeURIComponent(machine)}`
      : ''
  return get<{ items: AlarmItem[] }>(`/alarms/history?from=${from}&to=${to}${m}`)
}

export function apiShifts(from: number, to: number, machine: string) {
  return get<{ from: number; to: number; shifts: ShiftItem[] }>(
    `/stats/shifts?from=${from}&to=${to}&machine=${encodeURIComponent(machine)}`,
  )
}
