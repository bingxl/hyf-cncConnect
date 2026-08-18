# MTConnect 数据报表 Web API

服务端：`webserver.exe`（C++ / winsock + SQLite），默认端口 **8088**，数据源 `stats.db`
（由 `mtc_stats stream`（增量）或 `poll`（快照）持续写入）。生产环境同时托管前端构建产物
（`web/dist/`）。

**通用约定**

- Base URL: `http://<host>:8088`
- 时间参数 `from` / `to` 为 **Unix 秒**（UTC 毫秒 / 1000），省略 `to` 默认当前时间；
  省略 `from` 默认 24 小时前。
- `bucket` 为分桶秒数，默认 `1800`（30 分钟）。
- 响应均为 `application/json; charset=utf-8`，含 `Access-Control-Allow-Origin: *`（开发跨域）。
- 错误返回：`{"error": "<message>"}`，HTTP 400/404/500。
- 统计口径：
  - **加工中**：`execution=ACTIVE && mode=AUTOMATIC`（自动模式实际运行时间；`tmmode`
    仅作展示字段，不再硬性过滤，避免车铣复合 M 模式漏计；Mazak 无 tmmode 同样计入）
  - **加工时间(秒)**：相邻样本时间差累加（若样本 i 加工中，则累加 `ts[i+1]-ts[i]`，
    跨桶按桶边界拆分），对缺行/间隔不均比“样本数×平均间隔”稳健
  - **开机时间(秒)**：相邻样本时间差累加，样本 i 开机（`Availability=AVAILABLE`）则累加
    `ts[i+1]-ts[i]`；关机/断联（`UNAVAILABLE`）样本与超过 90s 的采样间隔不计入
  - **利用率**：`加工时间 / 开机时间`（开机时间为 0 时利用率为 0）
- **产量**：`part_total` **正增量求和**（相邻样本 `part_total` 上升则累加差值），
  兼容计数清零/重启（批次结束、断电、sim 重启等）；首尾差值会漏算
  - **产品**：按程序注释 `comment` 分组

---

## 1. 健康检查

```
GET /api/health
```

| 参数 | 类型 | 说明 |
|------|------|------|
| - | - | - |

**响应**

```json
{
  "status": "ok",
  "server_time": 1784095200,
  "db_rows": 43210,
  "first_sample": 1784060800,
  "last_sample": 1784095200
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| status | string | `ok` |
| server_time | int | 服务器当前 Unix 秒 |
| db_rows | int | samples 表行数 |
| first_sample / last_sample | int | 采样最早/最晚时间（无数据为 0） |

---

## 2. 机床列表

```
GET /api/machines
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| - | - | - | 返回 stats.db 中出现的全部机床 |

**响应**

```json
{
  "machines": [
    { "name": "ZXJ03", "first_ts": 1784060800, "last_ts": 1784095200 }
  ]
}
```

---

## 3. 时间段汇总（每台机床）

```
GET /api/stats/summary?from=1784060800&to=1784095200
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| from | int | 否 | 开始 Unix 秒，默认 24h 前 |
| to | int | 否 | 结束 Unix 秒，默认当前 |

**响应**

```json
{
  "from": 1784060800,
  "to": 1784095200,
  "interval_sec": 10,
  "items": [
    {
      "machine": "ZXJ03",
      "mach_sec": 61200,
      "power_sec": 72000,
      "util_rate": 0.85,
      "part_total_start": 332401,
      "part_total_end": 333055,
      "produced": 654,
      "sample_count": 4321,
      "machining_count": 3542,
      "last": { "execution": "ACTIVE", "mode": "AUTOMATIC", "comment": "(C-24-0000000-06332/A0)", "ts": 1784095190 }
    }
  ]
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| interval_sec | int | 实际采样间隔（= (last-first)/(count-1) 取整） |
| mach_sec | int | 加工时间（秒） |
| power_sec | int | 开机时间（秒，关机/断联时段不计入） |
| util_rate | float | 利用率 = mach_sec / power_sec（开机时间为 0 则为 0） |
| produced | int | 时间段内产量（part_total 正增量求和） |
| last | object | 最新一条采样状态（实时快照） |

---

## 4. 加工时间序列（分桶）

```
GET /api/stats/machining?from=&to=&machine=ZXJ03&bucket=1800
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| machine | string | 是 | 机床名；传 `ALL` 返回全厂各桶合计 |
| bucket | int | 否 | 分桶秒数，默认 1800 |

**响应**

```json
{
  "machine": "ZXJ03",
  "bucket": 1800,
  "interval_sec": 10,
  "points": [
    { "bucket_ts": 1784070000, "mach_sec": 1780, "power_sec": 1800,
      "machining_count": 178, "sample_count": 180 }
  ]
}
```

| 字段 | 说明 |
|------|------|
| bucket_ts | 桶起始 Unix 秒（对齐：`ts/bucket*bucket`） |
| mach_sec | 桶内加工秒数 |
| power_sec | 桶内开机秒数（关机/断联不计入） |
| machining_count / sample_count | 桶内加工中采样数 / 总采样数 |

## 5. 产量序列（分桶）

```
GET /api/stats/production?from=&to=&machine=ZXJ03&bucket=1800
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| machine | string | 是 | 机床名；传 `ALL` 返回全厂各桶合计 |
| bucket | int | 否 | 分桶秒数，默认 1800 |

**响应**

```json
{
  "machine": "ZXJ03",
  "bucket": 1800,
  "points": [
    { "bucket_ts": 1784070000, "produced": 42, "start_total": 332401, "end_total": 332443 }
  ]
}
```

| 字段 | 说明 |
|------|------|
| produced | 桶内产量（part_total 正增量求和，按机床分别累计） |
| start_total / end_total | 桶内首/末累计件数 |

---

## 6. 分产品产量（产品 = 程序注释）

```
GET /api/stats/products?from=&to=&machine=ZXJ03
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| machine | string | 是 | 机床名；传 `ALL` 时按全厂汇总（产品表通常按单机查看） |

**响应**

```json
{
  "machine": "ZXJ03",
  "items": [
    {
      "comment": "(C-24-0000000-06332/A0)",
      "produced": 654,
      "mach_sec": 61200,
      "start_total": 332401,
      "end_total": 333055,
      "first_ts": 1784060800,
      "last_ts": 1784095000
    }
  ]
}
```

> 同一种产品多次切换运行会合并累计；不同机床相同产品不合并（各算各的）。

---

## 7. 班次统计（白班/夜班）

```
GET /api/stats/shifts?from=&to=&machine=ALL
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| from / to | int | 是 | Unix 秒 |
| machine | string | 是 | 机床名；`ALL` 返回各机台明细 + 全厂合计 |

班次定义（AGENTS.md）：周一~周六 白班 08:30-20:30、夜班 20:30-次日 08:30；
周日 白班 08:30-17:00、无夜班。查询区间与班次取交集统计（`ws/we` 为裁剪后窗口）。

**响应**

```json
{
  "from": 1784060800, "to": 1784095200,
  "shifts": [
    {
      "shift": "day", "date": "2026-08-17", "date_ts": 1784291400,
      "start": 1784291400, "end": 1784334600, "ws": 1784291400, "we": 1784334600,
      "label": "白班 08:30-20:30",
      "machines": [
        { "machine": "ZXJ03", "mach_sec": 35000, "power_sec": 43200,
          "util_rate": 0.81, "produced": 120,
          "products": [ { "comment": "C-12-BRACKET-001/A0", "produced": 120 } ] }
      ],
      "fleet": { "mach_sec": 35000, "power_sec": 43200, "util_rate": 0.81,
                 "produced": 120, "products": [ { "comment": "C-12-BRACKET-001/A0", "produced": 120 } ] }
    }
  ]
}
```

| 字段 | 说明 |
|------|------|
| shift | `day` 白班 / `night` 夜班 |
| date / date_ts | 班次起始本地日期 |
| start / end | 完整班次区间；ws / we 为与查询区间相交的窗口 |
| machines | 各机台统计（加工时长/开机时长/利用率/产量/分产品件数） |
| fleet | 全厂合计（machine=ALL 时按机台汇总） |

---

## 8. 实时状态（转发 MTConnect agent）

```
GET /api/live/current
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| - | - | - | 从 agent `:5000/current` 拉取实时状态（不可达时 items 为空） |

**响应**

```json
{
  "items": [
    {
      "name": "ZXJ03",
      "available": true,
      "execution": "ACTIVE",
      "mode": "AUTOMATIC",
      "tmmode": "0",
      "program": "801.801",
      "comment": "(C-24-0000000-06332/A0)",
      "part_total": 333055,
      "ts": 1784095200
    }
  ]
}
```

---

## 8. 静态资源（前端）

```
GET /                    → web/dist/index.html
GET /assets/*            → web/dist/assets/*
GET /api/*               → API（其余路径若匹配 /api 前缀）
GET /{path}              → web/dist/{path}（SPA 回退 index.html）
```

前端构建产物放入 `web/dist/`，服务端自动托管；开发模式前端走 vite dev（`web/vite.config.ts` 配置 proxy `/api` → `:8088`）。

---

## 9. 报警查询（conditions / 急停）

报警由 `mtc_stats stream/poll` 从 agent 的条件（`<Fault>/<Warning>/<Failed>`）与
急停信号采集入库（`alarms` 表，每条 occurrence 以 machine+item_id+first_ts 唯一）。

```
GET /api/alarms/current
```

返回当前未恢复的报警：

```json
{
  "items": [
    {
      "machine": "ZXJ03",
      "item_id": "spindle",
      "item_type": "FAULT",
      "state": "FAULT",
      "first_ts": 1784095000,
      "last_ts": 1784095200,
      "end_ts": null,
      "active": 1
    }
  ]
}
```

```
GET /api/alarms/history?from=&to=&machine=
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| from / to | int | 否 | Unix 秒，默认最近 24h |
| machine | string | 否 | 机床名，省略为全部 |

返回与 /current 相同结构的列表（含已恢复报警，`end_ts` 非空、`active=0`）。

> 通知：`mtc_stats stream` 第 6/7 个参数为 `alert_url`（http(s) webhook，POST
> `{"text":"..."}`，兼容钉钉/企业微信机器人格式）和 `alert_min`（持续异常重复告警
> 间隔，默认 60 分钟）。触发场景：机床离线/恢复、采集服务（agent）断链/恢复。
