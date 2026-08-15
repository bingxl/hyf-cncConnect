# MTConnect 数据报表 Web API

服务端：`webserver.exe`（C / winsock + SQLite），默认端口 **8088**，数据源 `stats.db`（由
`mtc_stats poll` 持续写入）。生产环境同时托管前端构建产物（`web/dist/`）。

**通用约定**

- Base URL: `http://<host>:8088`
- 时间参数 `from` / `to` 为 **Unix 秒**（UTC 毫秒 / 1000），省略 `to` 默认当前时间；
  省略 `from` 默认 24 小时前。
- `bucket` 为分桶秒数，默认 `1800`（30 分钟）。
- 响应均为 `application/json; charset=utf-8`，含 `Access-Control-Allow-Origin: *`（开发跨域）。
- 错误返回：`{"error": "<message>"}`，HTTP 400/404/500。
- 统计口径：
  - **加工中**：`execution=ACTIVE && mode=AUTOMATIC && tmmode=0`（FANUC；Mazak 无 tmmode 则按前两项）
  - **加工时间(秒)**：加工中采样点数 × 采样间隔（当前 poller 为 10s）
  - **产量**：`part_total`（FANUC 参数 #6712 / 采集器映射）差值，负值钳为 0
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
      "total_time_sec": 34400,
      "util_rate": 0.82,
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
| total_time_sec | int | 有采样覆盖的时长（秒） |
| util_rate | float | 利用率 = mach_sec / total_time_sec（无数据为 0） |
| produced | int | 时间段内产量（part_total 末值 - 首值，钳 0） |
| last | object | 最新一条采样状态（实时快照） |

---

## 4. 加工时间序列（分桶）

```
GET /api/stats/machining?from=&to=&machine=ZXJ03&bucket=1800
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| machine | string | 是 | 机床名 |
| bucket | int | 否 | 分桶秒数，默认 1800 |

**响应**

```json
{
  "machine": "ZXJ03",
  "bucket": 1800,
  "interval_sec": 10,
  "points": [
    { "bucket_ts": 1784070000, "mach_sec": 1780, "machining_count": 178, "sample_count": 180 }
  ]
}
```

| 字段 | 说明 |
|------|------|
| bucket_ts | 桶起始 Unix 秒（对齐：`ts/bucket*bucket`） |
| mach_sec | 桶内加工秒数 |
| machining_count / sample_count | 桶内加工中采样数 / 总采样数 |

## 5. 产量序列（分桶）

```
GET /api/stats/production?from=&to=&machine=ZXJ03&bucket=1800
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| machine | string | 是 | 机床名 |
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
| produced | 桶内产量（桶内 part_total 首末差值，钳 0） |
| start_total / end_total | 桶内首/末累计件数 |

---

## 6. 分产品产量（产品 = 程序注释）

```
GET /api/stats/products?from=&to=&machine=ZXJ03
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| machine | string | 是 | 机床名 |

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

## 7. 实时状态（转发 MTConnect agent）

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
