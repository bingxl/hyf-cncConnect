# FANUC FOCAS2 库头文件 fwlib32.h 中文参考文档

> **库名称**：CNC/PMC Data Window Library for FOCAS  
> **版权**：Copyright (C) 2003-2017 by FANUC CORPORATION  
> **说明**：本文档基于 `fwlib32.h` 头文件翻译整理，涵盖数据结构、错误码、宏定义及全部 API 函数分类说明。

---

## 目录

1. [编译配置与平台宏](#1-编译配置与平台宏)
2. [系统常量定义](#2-系统常量定义)
3. [错误码定义](#3-错误码定义)
4. [DNC 操作结果码](#4-dnc-操作结果码)
5. [库选项名称](#5-库选项名称)
6. [核心数据结构](#6-核心数据结构)
7. [API 函数分类总览](#7-api-函数分类总览)
8. [各分类详细说明](#8-各分类详细说明)

---

## 1. 编译配置与平台宏

| 宏定义 | 说明 |
|--------|------|
| `_WIN32` / `_WIN32_WCE` | Windows 平台（含 Windows CE） |
| `_FWLIBDLL_` | 定义时为 DLL 导出模式，未定义时为 DLL 导入模式 |
| `F22_TYPEA` | 类型 A 配置（72 轴/16 主轴） |
| `F22_TYPEB` | 类型 B 配置（96 轴/24 主轴） |
| `F22_TYPE5` | 类型 5 配置（48 轴/16 主轴） |
| `CNC_PPC` | Power Mate 系列专用 |
| `CNC_SIM` | CNC 仿真模式 |
| `HSSB_LIB` | HSSB 高速串行总线库 |
| `FS30D` / `FS15D` / `FS0IDD` | 系列 30i/15i/0i-D 专用模式 |
| `FS15BD` | 系列 15i-B 专用模式 |
| `PMD` | Power Mate D/H 专用 |
| `PM_H` | Power Mate H 专用 |
| `ONO8D` | ONO8D 模式 |
| `PCD_UWORD` | PMC 控制器使用 unsigned short 类型 |

---

## 2. 系统常量定义

### 2.1 轴与主轴上限

根据编译配置宏不同，系统支持的最大轴数和主轴数如下：

| 配置 | MAX_AXIS（最大轴数） | MAX_SPINDLE（最大主轴数） |
|------|---------------------|------------------------|
| 默认（无特殊宏） | 32 | 8 |
| `F22_TYPE5` | 48 | 16 |
| `F22_TYPEA` | 72 | 16 |
| `F22_TYPEB` 或 非 PPC/SIM | 96 | 24 |

### 2.2 其他系统常量

| 常量 | 值 | 说明 |
|------|-----|------|
| `MAX_AXISNAME` | 4 | 轴名称最大字符数 |
| `ALL_AXES` | -1 | 表示"全部轴" |
| `ALL_SPINDLES` | -1 | 表示"全部主轴" |
| `MAX_IFSB_LINE` | 3 或 4 | IFSB 最大行号（取决于配置） |
| `MAX_CNCPATH` | 10 或 15 | 最大 CNC 路径数（取决于配置） |
| `MAX_LOCK_PROG` | 12 | 程序锁定最大数量 |

---

## 3. 错误码定义

FOCAS2 API 函数返回 `short` 类型的错误码。以下为完整错误码列表：

### 3.1 通信/系统级错误（负数）

| 错误码 | 常量名 | 中文说明 |
|--------|--------|----------|
| -17 | `EW_PROTOCOL` | 协议错误 |
| -16 | `EW_SOCKET` | Windows Socket 错误 |
| -15 | `EW_NODLL` | DLL 文件不存在 |
| -14 | `EW_INIERR` | API 库初始化文件错误 |
| -13 | `EW_ITLOW` | 智能终端低温报警 |
| -12 | `EW_ITHIGHT` | 智能终端高温报警 |
| -11 | `EW_BUS` | 总线错误 |
| -10 | `EW_SYSTEM2` | 系统错误 |
| -9 | `EW_HSSB` | HSSB 通信错误 |
| -8 | `EW_HANDLE` | Windows 库句柄错误 |
| -7 | `EW_VERSION` | CNC/PMC 版本不匹配 |
| -6 | `EW_UNEXP` | 异常错误 |
| -5 | `EW_SYSTEM` | 系统错误 |
| -4 | `EW_PARITY` | 共享 RAM 奇偶校验错误 |
| -3 | `EW_MMCSYS` | EMM386 或 MMCSYS 安装错误 |
| -2 | `EW_RESET` | 发生了复位或停止 |
| -1 | `EW_BUSY` | 忙碌（CNC 正在处理中） |

### 3.2 正常结果

| 错误码 | 常量名 | 中文说明 |
|--------|--------|----------|
| 0 | `EW_OK` | 正常（无错误） |

### 3.3 参数/数据级错误（正数）

| 错误码 | 常量名 | 中文说明 |
|--------|--------|----------|
| 1 | `EW_FUNC` | 命令准备错误 |
| 1 | `EW_NOPMC` | PMC 不存在 |
| 2 | `EW_LENGTH` | 数据块长度错误 |
| 3 | `EW_NUMBER` | 数据编号错误 |
| 3 | `EW_RANGE` | 地址范围错误 |
| 4 | `EW_ATTRIB` | 数据属性错误 |
| 4 | `EW_TYPE` | 数据类型错误 |
| 5 | `EW_DATA` | 数据错误 |
| 6 | `EW_NOOPT` | 无选件错误 |
| 7 | `EW_PROT` | 写保护错误 |
| 8 | `EW_OVRFLOW` | 内存溢出错误 |
| 9 | `EW_PARAM` | CNC 参数不正确 |
| 10 | `EW_BUFFER` | 缓冲区错误 |
| 11 | `EW_PATH` | 路径错误 |
| 12 | `EW_MODE` | CNC 模式错误 |
| 13 | `EW_REJECT` | 执行被拒绝 |
| 14 | `EW_DTSRVR` | 数据服务器错误 |
| 15 | `EW_ALARM` | 已发生报警 |
| 16 | `EW_STOP` | CNC 未在运行 |
| 17 | `EW_PASSWD` | 保护数据错误 |
| 18 | `EW_PMC` | PMC 产生的错误 |
| 19 | `EW_PMCHANDLE` | PMC 句柄错误 |
| 20 | `EW_RD_OVWSTP` | 程序读取中覆写停止 |
| 21 | `EW_RD_RSTFIN` | 程序读取中复位中断 |

---

## 4. DNC 操作结果码

| 结果码 | 常量名 | 中文说明 |
|--------|--------|----------|
| -1 | `DNC_NORMAL` | 正常完成 |
| -32768 | `DNC_CANCEL` | DNC 操作被 CNC 取消 |
| -514 | `DNC_OPENERR` | 文件打开错误 |
| -516 | `DNC_NOFILE` | 文件未找到 |
| -517 | `DNC_READERR` | 读取错误 |

---

## 5. 库选项名称

用于 `cnc_getlibopt` / `cnc_setlibopt` 函数的选项 ID：

| ID | 常量名 | 说明 |
|----|--------|------|
| 0 | `LIB_MODE` | 库模式 |
| 1 | `MOVE_RDPRGPTR` | 移动读取程序指针 |
| 2 | `PRM_ALLPATH` | 所有路径参数 |
| 3 | `UPLOAD_M02M99` | M02/M99 时上传 |
| 4 | `MSG_NOCTRL` | 无控制字符消息 |
| 5 | `DIAM_RAD_SWITCH` | 直径/半径切换 |
| 6 | `MSG_CONV` | 消息转换 |
| 7 | `ASYNC_READ_PROG3` | 异步读取程序 3 |
| 8 | `UP_DNLOAD_EDT` | 上/下载编辑 |
| 9 | `PROG_WORD_SRCH` | 程序字搜索 |
| 10 | `ONUM_ZERO_SUP` | 序号零抑制 |
| 11 | `LONG_ISE_FIG` | IS-E 长行程 |
| 12 | `INT_CHK_UNIT` | 中断检查单位 |
| 13 | `HZR_PRM_WR_SKIP` | 高速写参数跳过 |
| 14 | `SLVSRAM_ACCESS` | 从 SRAM 访问 |
| 15 | `GET_SMTCP_STAT` | 获取 SMTCP 状态 |
| 16 | `TLIFE_OPTION` | 刀具寿命选项 |
| 17 | `SVGD_MATE_PUNCH` | 进给同步穿孔 |
| 18 | `READ_FLD_ON` | 读取 FLD 开 |
| 19 | `DELETE_RECURSIVE` | 递归删除 |
| 20 | `READ_ORIG_OPT` | 读取原点选项 |
| 21 | `SVGD_MATE_ORIGIN` | 进给同步原点 |
| 22 | `PUN_SFZN_MDP` | 穿孔软限位 MDP |
| 23 | `PAXIS_PATH` | 物理轴路径 |
| 24 | `AXDATA_G198` | 轴数据 G198 |
| 25 | `BG_EDIT_SIGNAL` | 后台编辑信号 |
| 26 | `UPLOAD_BG` | 后台上传 |
| 27 | `TDATA_EXTRACT` | 工具数据提取 |
| 28 | `PROG_CHECK_CMNT` | 程序检查注释 |
| 29 | `INITIAL_AX_CONFIG` | 初始轴配置 |
| 30 | `MGI_SPECIFICATION` | MGI 规格 |
| 31 | `EFFECTIVE_COND` | 有效条件 |
| 32 | `LEVEL8_PROTECT` | 8 级保护 |
| 33 | `ACTPT_M198` | 执行指针 M198 |
| 34 | `SYSINFO_AXIS` | 系统信息轴 |
| 35 | `ALARM_INFO_TYPE` | 报警信息类型 |
| 36 | `PROG_LEDT_SPUP` | 程序 LEDT SPUP |
| 37 | `OPMSG_STATUS` | 操作消息状态 |
| 38 | `ASYNC_SEARCHWORD` | 异步搜索字 |
| 39 | `MA_OPT` | MA 选项 |
| 40 | `ENABLE_FOCAS_DMA` | 启用 FOCAS DMA |
| 41 | `DSHOST_RD_SRCH` | 数据服务器主机读搜索 |
| 42 | `BG_EDIT_CONTINUE` | 后台编辑继续 |
| 43 | `BG_EDIT_GRAPH` | 后台编辑图形 |
| 44 | `SEARCHWORD_PNTR` | 搜索字指针 |
| 45 | `PROG_UPLD_PROT` | 程序上传保护 |
| 46 | `POLAR_IPL_POS` | 极坐标 IPL 位置 |
| 47 | `PRG_NO_RD_PROT` | 程序号读保护 |
| 48 | `TOOL_STORAGE` | 刀具存储 |
| 49 | `PRG_FMT_CK` | 程序格式检查 |
| 50 | `NCPROG_MODE` | NC 程序模式 |
| 51 | `COMMAND_TIMEOUT` | 命令超时 |
| 64 | `PGLOCK_TYPE` | 程序锁类型 |
| 128 | `TLIFE_TOOL0` | 刀具寿命 Tool0 |
| 256 | `OPPROG_DSP` | 操作程序显示 |
| 512 | `OPPROG_MODE` | 操作程序模式 |
| 1024 | `PROGRAM_CHECK` | 程序检查 |
| 2048 | `CZPP_NEDPP` | CZPP/NEDPP |
| 4096 | `MULTI_PATH_MIX_AXIS_NAME` | 多路径混合轴名 |

### 工件设定常量

| 常量 | 值 | 说明 |
|------|-----|------|
| `WSETER_GRP` | 8 | 工件设定组数 |
| `WSETER_DATA` | 8 | 工件设定数据数 |

### 3D 干涉检查常量

| 常量 | 值 | 说明 |
|------|-----|------|
| `MAX_POS_BUF` | 2 | 最大位置缓冲区数 |

### 数据复制类型

| 常量 | 值 | 说明 |
|------|-----|------|
| `DC_PRM` | 1 | 参数 |
| `DC_OFS` | 2 | 偏置 |
| `DC_WKZ` | 3 | 工件零点偏置 |
| `DC_MAC` | 4 | 宏变量 |
| `DC_PCD` | 5 | P 代码宏变量 |
| `DC_RTM` | 6 | RTM 值 |

---

## 6. 核心数据结构

### 6.1 轴/主轴相关结构

#### ODBACT — 读取实际进给/主轴速度

```c
typedef struct odbact {
    short   dummy[2];   // 保留
    long    data;       // 实际进给值 / 实际主轴速度
} ODBACT;
```

**用于函数**：`cnc_actf`（读取实际进给）、`cnc_acts`（读取实际主轴速度）

#### ODBACT2 — 读取全部主轴速度

```c
typedef struct odbact2 {
    short   datano;                 // 主轴号
    short   type;                   // 保留
    long    data[MAX_SPINDLE];      // 各主轴数据
} ODBACT2;
```

**用于函数**：`cnc_acts2`

#### ODBAXIS — 读取轴位置数据

```c
typedef struct odbaxis {
    short   dummy;              // 保留
    short   type;               // 轴号
    long    data[MAX_AXIS];     // 数据值
} ODBAXIS;
```

**用于函数**：`cnc_absolute`、`cnc_machine`、`cnc_relative`、`cnc_distance`、`cnc_skip`、`cnc_srvdelay`、`cnc_accdecdly`、`cnc_absolute2`、`cnc_relative2`、`cnc_distance2`、`cnc_absolute_bg`、`cnc_relative_bg`、`cnc_machine_bg`

#### REALDATA — IEEE 双精度数据（IS-E 长行程）

```c
typedef struct realdata {
    double  val;    // 数据值
    long    dec;    // 小数点位数
    long    dummy;  // 保留
} REALDATA;
```

#### ODBAXIS64 — 双精度轴位置数据

```c
typedef struct odbaxis64 {
    short   dummy1;             // 保留
    short   type;               // 轴号
    short   dummy2[2];          // 保留
    REALDATA data[MAX_AXIS];    // 数据值
} ODBAXIS64;
```

**用于函数**：`cnc_machine2`（双精度版）

### 6.2 动态数据结构

#### ODBDY — 读取全部动态数据（旧版）

```c
typedef struct odbdy {
    short   dummy;
    short   axis;       // 轴号
    short   alarm;      // 报警状态
    short   prgnum;     // 当前程序号
    short   prgmnum;    // 主程序号
    long    seqnum;     // 当前序列号
    long    actf;       // 实际进给
    long    acts;       // 实际主轴速度
    union {
        struct {
            long    absolute[MAX_AXIS];    // 绝对位置
            long    machine[MAX_AXIS];     // 机械位置
            long    relative[MAX_AXIS];    // 相对位置
            long    distance[MAX_AXIS];    // 剩余移动量
        } faxis;
        struct {
            long    absolute;   // 绝对位置（单轴）
            long    machine;    // 机械位置
            long    relative;   // 相对位置
            long    distance;   // 剩余移动量
        } oaxis;
    } pos;
} ODBDY;
```

**用于函数**：`cnc_rddynamic`

#### ODBDY2 — 读取全部动态数据（扩展版）

```c
typedef struct odbdy2 {
    short   dummy;
    short   axis;       // 轴号
    long    alarm;      // 报警状态（扩展为 long）
    long    prgnum;     // 当前程序号
    long    prgmnum;    // 主程序号
    long    seqnum;     // 当前序列号
    long    actf;       // 实际进给
    long    acts;       // 实际主轴速度
    union {
        struct {
            long    absolute[MAX_AXIS];
            long    machine[MAX_AXIS];
            long    relative[MAX_AXIS];
            long    distance[MAX_AXIS];
        } faxis;
        struct {
            long    absolute;
            long    machine;
            long    relative;
            long    distance;
        } oaxis;
    } pos;
} ODBDY2;
```

**用于函数**：`cnc_rddynamic2`

#### ODBDY3 / ODBDY3M — 动态数据（版本 3）

与 ODBDY2 结构相同，用于 `cnc_rddynamic3` / `cnc_rddynamic3m`。其中 ODBDY3M 固定使用 32 轴数组。

### 6.3 位置/速度结构

#### POSELM — 位置元素

```c
typedef struct poselm {
    long    data;       // 位置数据
    short   dec;        // 小数位
    short   unit;       // 数据单位
    short   disp;       // 显示标志
    char    name;       // 轴名
    char    suff;       // 后缀
} POSELM;
```

#### ODBPOS — 读取刀具位置

```c
typedef struct odbpos {
    POSELM  abs;        // 绝对位置
    POSELM  mach;       // 机械位置
    POSELM  rel;        // 相对位置
    POSELM  dist;       // 距离
} ODBPOS;
```

**用于函数**：`cnc_rdposition`

#### SPEEDELM — 速度元素

```c
typedef struct speedelm {
    long    data;       // 速度数据
    short   dec;        // 小数位
    short   unit;       // 数据单位
    short   disp;       // 显示标志
    char    name;       // 数据名称
    char    suff;       // 后缀
} SPEEDELM;
```

#### ODBSPEED — 当前速度

```c
typedef struct odbspeed {
    SPEEDELM    actf;   // 实际进给率
    SPEEDELM    acts;   // type=1,-1:实际主轴速度; type=2,-2:铣削伺服速度
} ODBSPEED;
```

**用于函数**：`cnc_rdspeed`

#### LOADELM — 负载元素

```c
typedef struct loadelm {
    long    data;       // 负载表
    short   dec;        // 小数位
    short   unit;       // 单位
    char    name;       // 数据名称
    char    suff1;      // 后缀 1
    char    suff2;      // 后缀 2
    char    reserve;    // 保留
} LOADELM;
```

### 6.4 参数/设置数据结构

#### IODBPSD — 参数/设置数据

```c
typedef struct iodbpsd {
    short   datano;     // 数据号
    short   type;       // 轴号
    union {
        char    cdata;              // 参数/设置数据（char）
        short   idata;              // short 型数据
        long    ldata;              // long 型数据
        REALPRM rdata;              // 实数型数据
        char    cdatas[MAX_AXIS];   // 各轴 char 数据
        short   idatas[MAX_AXIS];   // 各轴 short 数据
        long    ldatas[MAX_AXIS];   // 各轴 long 数据
        REALPRM rdatas[MAX_AXIS];   // 各轴实数数据
    } u;
} IODBPSD;
```

**用于函数**：`cnc_rdparam`、`cnc_wrparam`、`cnc_rdset`、`cnc_wrset`

#### IODBPRM — 扩展参数读取

```c
typedef struct iodbprm {
    long    datano;     // 数据号
    short   type;       // 数据类型
    short   axis;       // 轴信息
    short   info;       // 杂项信息
    short   unit;       // 单位信息
    struct {
        long    prm_val;    // 参数值
        long    dec_val;    // 小数点
    } data[32];
} IODBPRM;
```

**用于函数**：`cnc_rdparam_ext`

### 6.5 宏变量结构

#### ODBM — 读取自定义宏变量

```c
typedef struct odbm {
    short   datano;     // 变量号
    short   dummy;      // 保留
    long    mcr_val;    // 宏变量值
    short   dec_val;    // 小数点
} ODBM;
```

**用于函数**：`cnc_rdmacro`

#### IODBMR — 读写宏变量区域

```c
typedef struct iodbmr {
    short   datano_s;   // 起始宏变量号
    short   dummy;      // 保留
    short   datano_e;   // 结束宏变量号
    struct {
        long    mcr_val;    // 宏变量值
        short   dec_val;    // 小数点
    } data[5];
} IODBMR;
```

**用于函数**：`cnc_rdmacror`、`cnc_wrmacror`

### 6.6 刀具偏置结构

#### ODBTOFS — 读取刀具偏置值

```c
typedef struct odbtofs {
    short   datano;     // 数据号
    short   type;       // 数据类型
    long    data;       // 数据
} ODBTOFS;
```

**用于函数**：`cnc_rdtofs`

#### IODBTO — 读写刀具偏置（区域指定）

```c
typedef struct iodbto {
    short   datano_s;   // 起始偏置号
    short   type;       // 偏置类型
    short   datano_e;   // 结束偏置号
    union {
        long    m_ofs[5];           // M 各独立
        long    m_ofs_a[5];         // M-A 全部
        long    m_ofs_b[10];        // M-B 全部
        long    m_ofs_c[20];        // M-C 全部
        // ... 更多子类型
    } u;
} IODBTO;
```

**用于函数**：`cnc_rdtofsr`、`cnc_wrtofsr`

### 6.7 工件零点偏置结构

#### IODBZOFS — 读写工件零点偏置

```c
typedef struct iodbzofs {
    short   datano;             // 偏置号
    short   type;               // 轴号
    long    data[MAX_AXIS];     // 数据值
} IODBZOFS;
```

**用于函数**：`cnc_rdzofs`、`cnc_wrzofs`

#### IODBZOR — 区域指定工件零点偏置

```c
typedef struct iodbzor {
    short   datano_s;               // 起始偏置号
    short   type;                   // 轴号
    short   datano_e;               // 结束偏置号
    long    data[8 * MAX_AXIS];     // 偏置值
} IODBZOR;
```

**用于函数**：`cnc_rdzofsr`、`cnc_wrzofsr`

### 6.8 DNC 诊断结构

#### ODBDNCDGN — DNC 操作诊断数据

```c
typedef struct odbdncdgn {
    short           ctrl_word;       // 控制字
    short           can_word;        // 取消字
    char            nc_file[16];     // NC 文件名
    unsigned short  read_ptr;        // 读指针
    unsigned short  write_ptr;       // 写指针
    unsigned short  empty_cnt;       // 空计数
    unsigned long   total_size;      // 总大小
} ODBDNCDGN;
```

### 6.9 程序相关结构

#### ODBPRO — 当前执行程序号

```c
typedef struct odbpro {
    short   dummy[2];   // 保留
    short   data;       // 正在运行的程序号
    short   mdata;      // 主程序号
} ODBPRO;
```

**用于函数**：`cnc_rdprgnum`

#### ODBEXEPRG — 正在执行的程序名

```c
typedef struct odbexeprg {
    char    name[36];   // 程序名
    long    o_num;      // 程序号
} ODBEXEPRG;
```

**用于函数**：`cnc_exeprgname`

#### ODBSEQ — 当前序列号

```c
typedef struct odbseq {
    short   dummy[2];   // 保留
    long    data;       // 序列号
} ODBSEQ;
```

**用于函数**：`cnc_rdseqnum`

#### PRGPNT — 执行程序指针

```c
typedef struct prgpnt {
    long    prog_no;    // 程序号
    long    blk_no;     // 块号
} PRGPNT;
```

**用于函数**：`cnc_rdexecpt`

### 6.10 主轴相关结构

#### ODBSPN — 串行主轴数据

```c
typedef struct odbspn {
    short   datano;                     // 主轴号
    short   type;                       // 保留
    short   data[MAX_SPINDLE];          // 主轴数据
} ODBSPN;
```

**用于函数**：`cnc_rdspload`、`cnc_rdspmaxrpm`、`cnc_rdspgear`

#### ODBCSS — 恒线速数据

```c
typedef struct odbcss {
    long    srpm;       // 指令主轴速度
    long    sspm;       // 指令恒定主轴速度
    long    smax;       // 指令最大主轴速度
} ODBCSS;
```

**用于函数**：`cnc_rdspcss`

### 6.11 程序目录结构

#### ODBNC — 程序信息

```c
typedef struct odbnc {
    union {
        struct {
            short   reg_prg;        // 已注册程序数
            short   unreg_prg;      // 未注册程序数
            long    used_mem;       // 已用内存
            long    unused_mem;     // 未用内存
        } bin;
        char    asc[31];            // ASCII 字符串
    } u;
} ODBNC;
```

**用于函数**：`cnc_rdproginfo`

#### ODBPRGNAME — 程序锁定状态

```c
typedef struct odbprgname {
    char    name[MAX_LOCK_PROG][256];   // 程序名数组
} ODBPRGNAME;
```

**用于函数**：`cnc_rdpdf_pglockstat`

### 6.12 PDF（程序目录文件）相关结构

#### ODBPDFDRV — 程序驱动器目录

```c
typedef struct odbpdfdrv {
    short   max_num;            // 最大驱动器号
    short   dummy;
    char    drive[16][12];      // 驱动器名
} ODBPDFDRV;
```

#### ODBPDFINF — 程序驱动器信息

```c
typedef struct odbpdfinf {
    long    used_page;      // 已用容量
    long    all_page;       // 总容量
    long    used_dir;       // 已用目录数
    long    all_dir;        // 总目录数
} ODBPDFINF;
```

#### ODBPDFADIR — 目录全部文件信息

```c
typedef struct odbpdfadir {
    short   data_kind;      // 数据类型
    short   year;           // 最后修改日期
    short   mon;
    short   day;
    short   hour;
    short   min;
    short   sec;
    short   dummy;
    long    dummy2;
    long    size;           // 文件大小
    unsigned long attr;     // 属性
    char    d_f[36];        // 路径名
    char    comment[52];    // 注释
    char    o_time[12];     // 加工时间戳
} ODBPDFADIR;
```

### 6.13 操作/手动进给结构

#### ODBJOGCMD — 手动数值指令

```c
typedef struct odbjogcmd {
    ODBJOGCMDCODE   gcode;          // G 代码
    ODBJOGCMDCODE   mcode;          // M 代码
    ODBJOGCMDCODE   scode;          // S 代码
    ODBJOGCMDCODE   tcode;          // T 代码
    ODBJOGCMDCODE   bcode;          // B 代码
    ODBJOGCMDCODE   padr;           // 地址 P
    ODBJOGCMDSCODE  extscode[4];    // 扩展 S 代码
    ODBJOGCMDAXIS   axis[MAX_AXIS]; // 轴数据
    long            axis_cnt;       // 轴计数
} ODBJOGCMD;
```

### 6.14 模拟/仿真结构

#### ODBSIML — 加工模拟数据

```c
typedef struct odbsiml {
    long    t_code;
    long    b_code;
    long    axis_no;
    long    machine[MAX_AXIS];
    long    dec[MAX_AXIS];
    long    fscsl;
} ODBSIML;
```

**用于函数**：`cnc_simulation`

### 6.15 速度/负载表结构

#### ODBSVLOAD — 伺服负载表

```c
typedef struct odbsvload {
    LOADELM svload;     // 伺服负载
} ODBSVLOAD;
```

**用于函数**：`cnc_rdsvmeter`

#### ODBSPLOAD — 主轴负载表

```c
typedef struct odbspload {
    LOADELM spload;     // 主轴负载
    LOADELM spspeed;    // 主轴速度
} ODBSPLOAD;
```

**用于函数**：`cnc_rdspmeter`

---

## 7. API 函数分类总览

FOCAS2 库共提供约 **1,900+** 个 API 函数，按功能分为以下类别：

| 分类编号 | 分类名称 | 函数数量 | 说明 |
|---------|----------|---------|------|
| 1 | 连接/工具 | 4 | 库初始化、连接管理、版本获取 |
| 2 | CNC 控制轴/主轴 | 48 | 轴位置、进给、主轴速度读取 |
| 3 | CNC 程序相关 | 216 | 程序上传/下载/搜索/编辑 |
| 4 | CNC NC 文件数据 | 112 | 参数、设置、偏置、宏变量读写 |
| 5 | CNC 刀具寿命管理 | 59 | 刀具寿命组、计数、数据读写 |
| 6 | CNC 刀具管理 | 59 | 刀具注册/删除、刀库管理 |
| 7 | CNC 操作历史 | 28 | 操作/报警历史记录读取 |
| 8 | CNC 3D 干涉检查 | 35 | 3D 碰撞检测形状数据 |
| 9 | CNC 故障诊断 | 18 | 高级诊断数据读取 |
| 10 | CNC 其他 | 491 | 系统信息、报警、面板信号等 |
| 11 | CNC 图形命令 | 10 | 图形位置读取 |
| 12 | CNC 伺服学习数据 | 43 | 伺服采样数据读写 |
| 13 | CNC NC 显示功能 | 8 | 屏幕操作 |
| 14 | CNC 远程诊断 | 10 | 远程诊断数据读写 |
| 15 | CNC FS18-LN 功能 | 4 | FS18 系列专用 |
| 16 | CNC FS31i-LNB 功能 | 10 | FS31i-B 系列专用 |
| 17 | CNC 教学数据接口 | 5 | 教学数据读取 |
| 18 | CNC C-EXE SRAM 文件 | 5 | C-EXE SRAM 文件操作 |
| 19 | CNC FSSB 系列 | 99 | FSSB/IFSB/DSA 等总线通信 |
| 20 | PMC 核心功能 | 57 | PMC 数据读写 |
| 21 | PMC 网络/通信 | 393 | PROFIBUS/以太网/数据服务器等 |
| 22 | 程序再启动 | 35 | 程序再启动点管理 |
| 23 | IS-E 长行程/MDD/ROBO 等 | 211 | 64 位数据、宏表、机器人等 |
| 24 | PMC 梯形图画面 | 5 | 梯形图屏幕操作 |

---

## 8. 各分类详细说明

### 8.1 连接与库管理

| 函数名 | 说明 |
|--------|------|
| `cnc_allclibhndl` | 通过 HSSB 打开连接，获取库句柄 |
| `cnc_allclibhndl2` | 通过以太网打开连接（版本 2） |
| `cnc_allclibhndl3` | 通过以太网打开连接（版本 3，指定端口） |
| `cnc_allclibhndl4` | 通过以太网打开连接（版本 4） |
| `cnc_freelibhndl` | 释放库句柄（关闭连接） |
| `cnc_settimeout` | 设置通信超时时间 |
| `cnc_resetconnect` | 重置连接 |
| `cnc_getfocas1opt` | 获取 FOCAS1 选项 |
| `cnc_rdetherinfo` | 读取以太网信息 |
| `cnc_getlibopt` | 获取库选项 |
| `cnc_setlibopt` | 设置库选项 |
| `cnc_getdllversion` | 获取 DLL 版本 |
| `cnc_startupprocess` | 启动进程（初始化） |
| `cnc_exitprocess` | 退出进程 |
| `cnc_exitthread` | 退出线程 |
| `cnc_rdnodenum` | 读取节点数量 |
| `cnc_rdnodeinfo` | 读取节点信息 |
| `cnc_setdefnode` | 设置默认节点 |

### 8.2 CNC 控制轴/主轴

| 函数名 | 说明 |
|--------|------|
| `cnc_sysinfo` | 读取系统信息（CNC 类型、系列、版本、最大轴数） |
| `cnc_statinfo` / `cnc_statinfo2` | 读取机器状态（运行模式、报警、急停等） |
| `cnc_actf` | 读取实际进给速度 |
| `cnc_acts` / `cnc_acts2` | 读取实际主轴速度（单/全部） |
| `cnc_absolute` | 读取绝对位置 |
| `cnc_machine` / `cnc_machine2` | 读取机械位置 |
| `cnc_relative` | 读取相对位置 |
| `cnc_distance` | 读取剩余移动量 |
| `cnc_skip` | 读取跳步位置 |
| `cnc_srvdelay` | 读取伺服延迟值 |
| `cnc_accdecdly` | 读取加减速延迟值 |
| `cnc_rddynamic` / `cnc_rddynamic2` / `cnc_rddynamic3` | 读取全部动态数据 |
| `cnc_wrrelpos` | 设置原点/预设相对位置 |
| `cnc_prstwkcd` | 预设工件坐标 |
| `cnc_rdmovrlap` | 读取手动重叠运动值 |
| `cnc_rdspload` | 读取串行主轴负载信息 |
| `cnc_rdspmaxrpm` | 读取主轴最大转速比 |
| `cnc_rdspgear` | 读取主轴齿轮比 |
| `cnc_rdposition` | 读取刀具位置（详细） |
| `cnc_rdspeed` | 读取当前速度 |
| `cnc_rdsvmeter` | 读取伺服负载表 |
| `cnc_rdspmeter` | 读取主轴负载表 |
| `cnc_rdhndintrpt` | 读取手轮中断 |
| `cnc_rd5axmandt` | 读取 5 轴加工手动进给 |
| `cnc_rdspcss` | 读取恒线速数据 |
| `cnc_rdexecpt` / `cnc_rdexecptm` | 读取执行程序指针 |
| `cnc_rdjogdrun` | 读取点动/干运行进给速度 |
| `cnc_rdaxisdata` | 读取各种轴数据 |
| `cnc_simulation` | 读取加工模拟数据 |
| `cnc_rdaxisname` | 读取轴名称 |
| `cnc_rdspdlname` | 读取主轴名称 |
| `cnc_axisnum` / `cnc_axisnum2` | 读取轴数量 |
| `cnc_loadtorq` | 读取负载转矩 |
| `cnc_rdactspdl` | 读取实际主轴数据 |

### 8.3 CNC 程序相关

#### 程序传输

| 函数名 | 说明 |
|--------|------|
| `cnc_dwnstart` / `cnc_dwnstart3` / `cnc_dwnstart4` | 开始下载程序 |
| `cnc_download` / `cnc_download3` / `cnc_download4` | 下载程序数据 |
| `cnc_dwnend` / `cnc_dwnend3` / `cnc_dwnend4` | 结束下载 |
| `cnc_upstart` / `cnc_upstart3` / `cnc_upstart4` | 开始上传程序 |
| `cnc_upload` / `cnc_upload3` / `cnc_upload4` | 上传程序数据 |
| `cnc_upend` / `cnc_upend3` / `cnc_upend4` | 结束上传 |
| `cnc_vrfstart` / `cnc_vrfstart4` | 开始校验程序 |
| `cnc_verify` / `cnc_verify4` | 校验程序数据 |
| `cnc_vrfend` / `cnc_vrfend4` | 结束校验 |
| `cnc_dncstart` / `cnc_dncstart2` | 开始 DNC 操作 |
| `cnc_dnc` / `cnc_dnc2` | DNC 传输数据 |
| `cnc_dncend` / `cnc_dncend2` | 结束 DNC 操作 |

#### 程序文件操作（PDF）

| 函数名 | 说明 |
|--------|------|
| `cnc_fileread_start` / `cnc_fileread` / `cnc_fileread_end` | 文件读取 |
| `cnc_filewrite_start` / `cnc_filewrite` / `cnc_filewrite_end` | 文件写入 |
| `cnc_punch_prog` / `cnc_punch_prog2` / `cnc_punch_prog3` | 输出程序 |
| `cnc_read_prog` / `cnc_read_prog2` / `cnc_read_prog3` | 读取程序 |
| `cnc_pdf_punch` / `cnc_pdf_read` | PDF 输出/读取 |
| `cnc_pdf_add` | 创建 PDF 文件 |
| `cnc_pdf_del` | 删除 PDF 文件 |
| `cnc_pdf_rename` | 重命名 PDF 文件 |
| `cnc_pdf_copy` | 复制 PDF 文件 |
| `cnc_pdf_move` | 移动 PDF 文件 |
| `cnc_pdf_cond` | PDF 文件压缩 |
| `cnc_pdf_dncset` / `cnc_pdf_dncset2` | PDF DNC 设置 |
| `cnc_pdf_dncread` | PDF DNC 读取 |
| `cnc_pdf_mergeprog` | 合并程序 |
| `cnc_verify_prog` | 校验程序 |
| `cnc_punch_data` / `cnc_read_data` | 数据输出/读取 |

#### 程序管理

| 函数名 | 说明 |
|--------|------|
| `cnc_rdprogdir` / `cnc_rdprogdir2` / `cnc_rdprogdir3` / `cnc_rdprogdir4` | 读取程序目录 |
| `cnc_rdproginfo` | 读取程序信息 |
| `cnc_rdprgnum` | 读取当前执行程序号 |
| `cnc_exeprgname` / `cnc_exeprgname2` | 读取执行中程序名 |
| `cnc_dncprgname` | 读取 DNC 程序名 |
| `cnc_rdseqnum` | 读取当前序列号 |
| `cnc_seqsrch` / `cnc_seqsrch2` | 序列号搜索 |
| `cnc_rewind` | 程序回卷 |
| `cnc_rdblkcount` | 读取块计数 |
| `cnc_rdexecprog` / `cnc_rdexecprog2` / `cnc_rdexecprog3` | 读取执行中程序 |
| `cnc_newprog` | 创建新程序 |
| `cnc_copyprog` | 复制程序 |
| `cnc_renameprog` | 重命名程序 |
| `cnc_condense` | 程序压缩 |
| `cnc_mergeprog` | 合并程序 |
| `cnc_delete` | 删除程序 |
| `cnc_delall` | 删除全部程序 |
| `cnc_delrange` | 范围删除 |
| `cnc_search` / `cnc_search2` | 程序搜索 |
| `cnc_rdactpt` / `cnc_wractpt` | 读写执行指针 |
| `cnc_rdprogline` / `cnc_rdprogline2` | 读取程序行 |
| `cnc_wrprogline` | 写入程序行 |
| `cnc_delprogline` | 删除程序行 |
| `cnc_searchword` / `cnc_searchword2` | 程序字搜索 |
| `cnc_setpglock` / `cnc_resetpglock` | 设置/重置程序锁 |
| `cnc_rdpglockstat` | 读取程序锁状态 |
| `cnc_rdprotect` / `cnc_rdprotect2` | 读取保护状态 |

### 8.4 CNC NC 文件数据（参数/偏置/宏变量）

#### 参数

| 函数名 | 说明 |
|--------|------|
| `cnc_rdparam` / `cnc_rdparam3` | 读取参数 |
| `cnc_wrparam` / `cnc_wrparam3` | 写入参数 |
| `cnc_rdparar` / `cnc_rdparar3` | 区域读取参数 |
| `cnc_wrparas` / `cnc_wrparas3` | 多个参数写入 |
| `cnc_rdparam_ext` | 扩展参数读取 |
| `cnc_preset_prm` | 参数预设 |
| `cnc_validate_prm` / `cnc_cancel_prm` | 参数验证/取消 |
| `cnc_start_async_wrparam` / `cnc_end_async_wrparam` | 异步参数写入 |
| `cnc_rdcncid` | 读取 CNC ID |
| `cnc_rdhsprminfo` / `cnc_rdhsparam` | 高速参数读取 |

#### 设置数据

| 函数名 | 说明 |
|--------|------|
| `cnc_rdset` | 读取设置数据 |
| `cnc_wrset` | 写入设置数据 |
| `cnc_rdsetr` | 区域读取设置 |
| `cnc_wrsets` | 多个设置写入 |

#### 偏置

| 函数名 | 说明 |
|--------|------|
| `cnc_rdtofs` / `cnc_wrtofs` | 读/写刀具偏置 |
| `cnc_rdtofsr` / `cnc_wrtofsr` | 区域读/写刀具偏置 |
| `cnc_clrtofs` | 清除刀具偏置 |
| `cnc_rdzofs` / `cnc_wrzofs` | 读/写工件零点偏置 |
| `cnc_rdzofsr` / `cnc_wrzofsr` | 区域读/写工件零点偏置 |
| `cnc_tofs_rnge` | 刀具偏置有效范围 |
| `cnc_zofs_rnge` | 工件零点偏置有效范围 |
| `cnc_wksft_rnge` | 工件移动值有效范围 |

#### 螺距误差补偿

| 函数名 | 说明 |
|--------|------|
| `cnc_rdpitchr` / `cnc_wrpitchr` | 读/写螺距误差补偿 |
| `cnc_rdpitchr2` / `cnc_wrpitchr2` | 螺距误差补偿（版本 2） |
| `cnc_checkpitch` | 检查螺距误差补偿 |
| `cnc_rdhipitchr` / `cnc_wrhipitchr` | 高精度螺距误差补偿 |
| `cnc_rdpitchinfo` | 螺距补偿信息 |
| `cnc_rdpitchblkinfo` | 螺距补偿块信息 |

#### 宏变量

| 函数名 | 说明 |
|--------|------|
| `cnc_rdmacro` / `cnc_wrmacro` | 读/写自定义宏变量 |
| `cnc_rdmacro2` / `cnc_rdmacro3` | 读取宏变量（版本 2/3） |
| `cnc_rdmacror` / `cnc_wrmacror` | 区域读/写宏变量 |
| `cnc_rdmacror2` / `cnc_rdmacror2_name` | 读取宏变量（带名称） |
| `cnc_rdmacror3` / `cnc_rdmacror4` | 读取宏变量（版本 3/4） |
| `cnc_rdmacronum` | 读取宏变量号 |
| `cnc_rdmacroinfo` | 读取宏变量信息 |
| `cnc_rdmacro_bg` / `cnc_wrmacro_bg` | 后台读/写宏变量 |

#### P 代码宏变量

| 函数名 | 说明 |
|--------|------|
| `cnc_rdpmacro` / `cnc_wrpmacro` | 读/写 P 代码宏变量 |
| `cnc_rdpmacror` / `cnc_wrpmacror` | 区域读/写 P 代码宏变量 |
| `cnc_rdpmacror2` / `cnc_wrpmacror2` | P 代码宏变量（版本 2） |
| `cnc_rdpmacroinfo` / `cnc_rdpmacroinfo2` / `cnc_rdpmacroinfo3` | P 代码宏变量信息 |

#### 体积补偿

| 函数名 | 说明 |
|--------|------|
| `cnc_rdvolc` / `cnc_wrvolc` | 读/写体积补偿数据 |
| `cnc_rdvolccomp` | 读取体积补偿量 |
| `cnc_rdrotvolc` / `cnc_wrrotvolc` | 读/写旋转体积补偿 |

#### 刀具几何尺寸

| 函数名 | 说明 |
|--------|------|
| `cnc_rdtlgeomsize` / `cnc_wrtlgeomsize` | 读/写刀具几何尺寸 |
| `cnc_rdtlgeomsize_ext` / `cnc_wrtlgeomsize_ext` | 扩展刀具几何尺寸 |
| `cnc_rdtlgeomsize_ext2` / `cnc_wrtlgeomsize_ext2` | 扩展刀具几何尺寸（版本 2） |

#### 诊断数据

| 函数名 | 说明 |
|--------|------|
| `cnc_diagnoss` | 读取诊断数据 |
| `cnc_diagnosr` | 区域读取诊断数据 |
| `cnc_rddiag_ext` | 扩展诊断数据读取 |

### 8.5 CNC 刀具寿命管理

| 函数名 | 说明 |
|--------|------|
| `cnc_rdgrpid` / `cnc_rdgrpid2` | 读取刀具寿命组 ID |
| `cnc_rdngrp` | 读取组号 |
| `cnc_rdntool` | 读取组内刀具数 |
| `cnc_rdlife` | 读取刀具寿命 |
| `cnc_rdcount` | 读取刀具计数 |
| `cnc_rd1length` / `cnc_rd2length` | 读取刀具长度偏置 |
| `cnc_rd1radius` / `cnc_rd2radius` | 读取刀具半径偏置 |
| `cnc_rdtoolrng` | 读取刀具范围数据 |
| `cnc_rdtoolgrp` | 读取刀具组数据 |
| `cnc_wrcountr` | 写入计数器 |
| `cnc_rdusegrpid` | 读取使用中的组 ID |
| `cnc_rdmaxgrp` | 读取最大组数 |
| `cnc_rdmaxtool` | 读取最大刀具数 |
| `cnc_rdusetlno` | 读取使用中的刀具号 |
| `cnc_rd1tlifedata` / `cnc_rd2tlifedata` | 读取刀具寿命数据 |
| `cnc_wr1tlifedata` / `cnc_wr2tlifedata` | 写入刀具寿命数据 |
| `cnc_rdgrpinfo` ~ `cnc_rdgrpinfo4` | 读取组信息 |
| `cnc_wrgrpinfo` ~ `cnc_wrgrpinfo3` | 写入组信息 |
| `cnc_deltlifegrp` | 删除刀具寿命组 |
| `cnc_instlifedt` | 插入寿命数据 |
| `cnc_deltlifedt` | 删除寿命数据 |
| `cnc_clrcntinfo` | 清除计数信息 |

### 8.6 CNC 刀具管理

| 函数名 | 说明 |
|--------|------|
| `cnc_regtool` / `cnc_regtool_f2` | 注册刀具 |
| `cnc_deltool` | 删除刀具 |
| `cnc_rdtool` / `cnc_rdtool2` / `cnc_rdtool_f2` | 读取刀具数据 |
| `cnc_wrtool` / `cnc_wrtool2` / `cnc_wrtool_f2` | 写入刀具数据 |
| `cnc_regmagazine` | 注册刀库 |
| `cnc_delmagazine` | 删除刀库 |
| `cnc_rdmagazine` / `cnc_wrmagazine` | 读/写刀库数据 |
| `cnc_rdctname` | 读取刀具类型名称 |
| `cnc_rdtlname` | 读取刀具名称 |
| `cnc_btlfpotsrh` | 刀套搜索 |
| `cnc_tool_in` / `cnc_tool_out` | 刀具装入/卸出 |
| `cnc_tool_temp_in` / `cnc_tool_temp_out` | 临时装入/卸出 |
| `cnc_tool_move` | 刀具移动 |
| `cnc_toolsrch` | 刀具搜索 |
| `cnc_magazinesrch` | 刀库搜索 |
| `cnc_rdmag_property` / `cnc_wrmag_property` | 读/写刀库属性 |
| `cnc_rdpot_property` / `cnc_wrpot_property` | 读/写刀套属性 |

### 8.7 CNC 操作历史

| 函数名 | 说明 |
|--------|------|
| `cnc_stopophis` / `cnc_startophis` | 停止/开始操作历史记录 |
| `cnc_rdophisno` / `cnc_rdophistry` | 读取操作历史 |
| `cnc_rdalmhisno` / `cnc_rdalmhistry` | 读取报警历史 |
| `cnc_clearophis` | 清除操作历史 |
| `cnc_backupophis` | 备份操作历史 |
| `cnc_rdhissgnl` / `cnc_wrhissgnl` | 读/写历史信号 |

### 8.8 CNC 3D 干涉检查

| 函数名 | 说明 |
|--------|------|
| `cnc_rdtdiinfo` | 读取 3D 干涉检查信息 |
| `cnc_rdtdinamesetting` / `cnc_wrtdinamesetting` | 读/写名称设置 |
| `cnc_rdtdifignum` / `cnc_wrtdifignum` | 读/写图形编号 |
| `cnc_rdtdishapedata` / `cnc_wrtdishapedata` | 读/写形状数据 |
| `cnc_rdtdicubedata` / `cnc_wrtdicubedata` | 读/写立方体数据 |
| `cnc_rdtdicylinderdata` / `cnc_wrtdicylinderdata` | 读/写圆柱体数据 |
| `cnc_rdtdiplanedata` / `cnc_wrtdiplanedata` | 读/写平面数据 |
| `cnc_rdtdicomment` / `cnc_wrtdicomment` | 读/写注释 |
| `cnc_rdtdicolordata` / `cnc_wrtdicolordata` | 读/写颜色数据 |

### 8.9 CNC 故障诊断

| 函数名 | 说明 |
|--------|------|
| `cnc_mdg_rdalmnum` | 读取报警数量 |
| `cnc_mdg_rdalminfo` | 读取报警信息 |
| `cnc_mdg_rdmsg` | 读取消息 |
| `cnc_mdg_rdflow` | 读取流程数据 |
| `cnc_mdg_rddtmsg` | 读取详细消息 |
| `cnc_mdg_rdmsgordr` | 读取消息顺序 |
| `cnc_mdg_rdcontinfo` | 读取上下文信息 |
| `cnc_mdg_rdlatchedalm` | 读取锁存报警 |
| `cnc_mdg_rdalminfoview2` | 读取报警信息视图 2 |
| `cnc_mdg_rdwvdata` | 读取波形数据 |
| `cnc_mdg_rdheatsimlt` | 读取热模拟数据 |
| `cnc_mdg_rdloadlvl` | 读取负载等级 |
| `cnc_mdg_monistat` / `cnc_mdg_moniclear` | 监控状态/清除 |
| `cnc_mdg_rdsysinfo` | 读取系统信息 |

### 8.10 CNC 其他功能（核心）

#### 系统信息/状态

| 函数名 | 说明 |
|--------|------|
| `cnc_sysinfo` | 读取系统信息 |
| `cnc_statinfo` / `cnc_statinfo2` | 读取机器状态 |
| `cnc_alarm` / `cnc_alarm2` | 读取报警状态 |
| `cnc_clearalm` | 清除报警 |
| `cnc_rdalminfo` / `cnc_rdalminfo2` | 读取报警信息 |
| `cnc_rdalmmsg` / `cnc_rdalmmsg2` / `cnc_rdalmmsg3` | 读取报警消息 |
| `cnc_clralm` | 清除报警 |
| `cnc_getcncmodel` | 获取 CNC 型号 |

#### 模态/G 代码

| 函数名 | 说明 |
|--------|------|
| `cnc_modal` | 读取模态数据 |
| `cnc_cannedcycle` | 读取固定循环 |
| `cnc_rdgcode` / `cnc_rdgcodem` | 读取 G 代码 |
| `cnc_block_status` | 读取块状态 |
| `cnc_rdcommand` | 读取命令值 |
| `cnc_rdmodalval` | 读取模态值 |

#### 操作者消息

| 函数名 | 说明 |
|--------|------|
| `cnc_rdopmsg` / `cnc_rdopmsg2` / `cnc_rdopmsg3` | 读取操作者消息 |
| `cnc_rdlnopmsg` | 读取行操作者消息 |

#### 路径/画面

| 函数名 | 说明 |
|--------|------|
| `cnc_setpath` / `cnc_getpath` | 设置/获取当前路径 |
| `cnc_getcrntscrn` / `cnc_slctscrn` | 获取/选择当前画面 |

#### 面板信号

| 函数名 | 说明 |
|--------|------|
| `cnc_rdopnlsgnl` / `cnc_wropnlsgnl` | 读/写面板信号 |
| `cnc_rdopnlgnrl` / `cnc_wropnlgnrl` | 读/写面板通用信号 |
| `cnc_rdopnlgsname` / `cnc_wropnlgsname` | 读/写面板组信号名 |

#### FROM/SRAM

| 函数名 | 说明 |
|--------|------|
| `cnc_rdfrominfo` / `cnc_getfrominfo` | 读取 FROM 信息 |
| `cnc_fromsvstart` / `cnc_fromsave` / `cnc_fromsvend` | FROM 保存 |
| `cnc_fromldstart` / `cnc_fromload` / `cnc_fromldend` | FROM 加载 |
| `cnc_rdsraminfo` / `cnc_getsraminfo` | 读取 SRAM 信息 |
| `cnc_srambkstart` / `cnc_srambackup` / `cnc_srambkend` | SRAM 备份 |
| `cnc_sramgetstart` / `cnc_sramget` / `cnc_sramgetend` | SRAM 读取 |
| `cnc_sramputstart` / `cnc_sramput` / `cnc_sramputend` | SRAM 写入 |

#### 数据服务器

| 函数名 | 说明 |
|--------|------|
| `cnc_dtsvftpget` / `cnc_dtsvftpput` | FTP 传输 |
| `cnc_dtsvrdpgdir` | 读取程序目录 |
| `cnc_dtsvdelete` | 删除文件 |
| `cnc_dtsvdownload` / `cnc_dtsvupload` | 下载/上传 |
| `cnc_dtsvrdset` / `cnc_dtsvwrset` | 读/写设置 |
| `cnc_dtsvchkdsk` / `cnc_dtsvhdformat` | 检查/格式化硬盘 |

#### 波形采样

| 函数名 | 说明 |
|--------|------|
| `cnc_rdwaveprm` / `cnc_wrwaveprm` | 读/写波形参数 |
| `cnc_wavestart` / `cnc_wavestop` / `cnc_wavestat` | 开始/停止/状态 |
| `cnc_rdwavedata` / `cnc_rdwavedata2` / `cnc_rdwavedata3` | 读取波形数据 |

#### 伺服/主轴详细

| 函数名 | 说明 |
|--------|------|
| `cnc_rdloopgain` | 读取环路增益 |
| `cnc_rdcurrent` | 读取电流 |
| `cnc_rdsrvspeed` | 读取伺服速度 |
| `cnc_rdposerrs` / `cnc_rdposerrs2` | 读取位置误差 |
| `cnc_rdposerrz` | 读取 Z 相位置误差 |
| `cnc_rdsynerrsy` / `cnc_rdsynerrrg` | 读取同步误差 |
| `cnc_rdspdlalm` | 读取主轴报警 |
| `cnc_rdctrldi` / `cnc_rdctrldo` | 读取控制 I/O |
| `cnc_rdnspdl` | 读取主轴数 |

#### 通信

| 函数名 | 说明 |
|--------|------|
| `cnc_rdcomparam` / `cnc_wrcomparam` | 读/写通信参数 |
| `cnc_rdcomlogmsg` | 读取通信日志消息 |
| `cnc_sendmessage` | 发送消息 |
| `cnc_rdrcvmsg` / `cnc_rdsndmsg` | 读取接收/发送消息 |

#### 工件坐标偏移

| 函数名 | 说明 |
|--------|------|
| `cnc_rdwkcdshft` / `cnc_wrwkcdshft` | 读/写工件坐标偏移 |
| `cnc_rdwkcdsfms` / `cnc_wrwkcdsfms` | 读/写工件坐标偏移（测量） |

#### HPCC（高精度轮廓控制）

| 函数名 | 说明 |
|--------|------|
| `cnc_rdhpccset` / `cnc_wrhpccset` | 读/写 HPCC 设置 |
| `cnc_hpccatset` | HPCC 自动设置 |
| `cnc_rdhpcctupr` / `cnc_wrhpcctupr` | 读/写调整参数 |

#### 安全区域

| 函数名 | 说明 |
|--------|------|
| `cnc_rdsafetyzone` / `cnc_wrsafetyzone` | 读/写安全区域 |
| `cnc_rdtoolzone` / `cnc_wrtoolzone` | 读/写刀具区域 |
| `cnc_rdacttlzone` | 读取当前刀具区域 |

#### 起动/复位

| 函数名 | 说明 |
|--------|------|
| `cnc_start` | 起动 CNC |
| `cnc_reset` / `cnc_reset2` | 复位 CNC |
| `cnc_dispoptmsg` / `cnc_optmsgans` | 显示选项消息/应答 |

### 8.11 PMC 核心功能

| 函数名 | 说明 |
|--------|------|
| `pmc_rdpmcrng` / `pmc_wrpmcrng` | 读/写 PMC 数据范围 |
| `pmc_wrpmcrng2` | PMC 数据写入（版本 2） |
| `pmc_rdwrpmcrng` | PMC 数据读写 |
| `pmc_rdkpm` / `pmc_wrkpm` | 读/写保持型 PMC 数据 |
| `pmc_kpmsiz` | 保持型 PMC 数据大小 |
| `pmc_rdpmcinfo` | 读取 PMC 信息 |
| `pmc_rdcntldata` / `pmc_wrcntldata` | 读/写控制数据 |
| `pmc_rdcntlgrp` / `pmc_wrcntlgrp` | 读/写控制组 |
| `pmc_rdalmmsg` | 读取 PMC 报警消息 |
| `pmc_getdtailerr` | 获取详细错误 |
| `pmc_rdpmcmem` / `pmc_wrpmcmem` | 读/写 PMC 内存 |
| `pmc_rdpmcsemem` / `pmc_wrpmcsemem` | 读/写 PMC 扩展内存 |
| `pmc_rdpmctitle` / `pmc_rdpmctitle2` | 读取 PMC 标题 |
| `pmc_rdprmstart` / `pmc_rdpmcparam` / `pmc_rdprmend` | 读取 PMC 参数 |
| `pmc_wrprmstart` / `pmc_wrpmcparam` / `pmc_wrprmend` | 写入 PMC 参数 |
| `pmc_rdpmcrng_ext` | 扩展 PMC 数据范围读取 |
| `pmc_wriolinkdat` | 写入 I/O 链路数据 |
| `pmc_rdpmcaddr` | 读取 PMC 地址 |
| `pmc_select_pmc_unit` / `pmc_get_current_pmc_unit` | 选择/获取 PMC 单元 |
| `pmc_get_number_of_pmc` | 获取 PMC 数量 |
| `pmc_get_pmc_unit_types` | 获取 PMC 单元类型 |
| `pmc_rdmsg` / `pmc_wrmsg` | 读/写消息 |
| `pmc_crdmsg` / `pmc_cwrmsg` | 条件读/写消息 |
| `pmc_rdioconfigtitle` | 读取 I/O 配置标题 |
| `pmc_rdmessagetitle` | 读取消息标题 |

### 8.12 PMC 网络通信

#### PROFIBUS

| 函数名 | 说明 |
|--------|------|
| `pmc_prfrdinfo` | 读取 PROFIBUS 信息 |
| `pmc_prfrdconfig` | 读取配置 |
| `pmc_prfrdbusprm` / `pmc_prfwrbusprm` | 读/写总线参数 |
| `pmc_prfrdslvprm` / `pmc_prfwrslvprm` | 读/写从站参数 |
| `pmc_prfrdallcadr` / `pmc_prfwrallcadr` | 读/写全部站地址 |
| `pmc_prfrdslvaddr` / `pmc_prfwrslvaddr` | 读/写从站地址 |
| `pmc_prfrdslvstat` | 读取从站状态 |
| `pmc_prfrddido` / `pmc_prfwrdido` | 读/写 DI/DO |
| `pmc_prfrdopmode` / `pmc_prfwropmode` | 读/写操作模式 |

#### 数据服务器（ds_ 前缀）

| 函数名 | 说明 |
|--------|------|
| `ds_rdmode` / `ds_wrmode` | 读/写模式 |
| `ds_rdhddinfo` | 读取硬盘信息 |
| `ds_rdhdddir` | 读取硬盘目录 |
| `ds_delhddfile` | 删除硬盘文件 |
| `ds_copyhddfile` / `ds_renhddfile` | 复制/重命名硬盘文件 |
| `ds_puthddfile` / `ds_gethostfile` | 上传/下载文件 |
| `ds_rdhostinfo` / `ds_rdhostdir` | 读取主机信息/目录 |
| `ds_rdncfile` / `ds_wrncfile` | 读/写 NC 文件 |
| `ds_checkhdd` / `ds_formathdd` | 检查/格式化硬盘 |
| `ds_searchword` / `ds_searchresult` | 搜索字/结果 |

#### 以太网（eth_ 前缀）

| 函数名 | 说明 |
|--------|------|
| `eth_rdparam` / `eth_wrparam` | 读/写以太网参数 |
| `eth_rdembdev` / `eth_wrembdev` | 读/写嵌入式设备 |
| `eth_ping` / `eth_ping_result` / `eth_ping_cancel` | Ping 操作 |
| `eth_rddsmode` / `eth_wrdsmode` | 读/写数据服务器模式 |
| `eth_rdlsistate` / `eth_clrlsistate` | 读/清除链路状态 |
| `eth_rdlog` / `eth_clrlog` | 读/清除日志 |

#### PROFINET（pbm_/pbs_ 前缀）

| 函数名 | 说明 |
|--------|------|
| `pbm_rd_param` / `pbm_wr_param` | 读/写 PROFINET 主站参数 |
| `pbm_ini_prm` | 初始化参数 |
| `pbm_rd_allslvtbl` | 读取全部从站表 |
| `pbm_rd_nodeinfo` / `pbm_rd_slot` | 读取节点/插槽信息 |
| `pbs_rd_param` / `pbs_wr_param` | 读/写 PROFINET 从站参数 |

#### DeviceNet（dnm_/dns_ 前缀）

| 函数名 | 说明 |
|--------|------|
| `dnm_rdparam` / `dnm_wrparam` | 读/写 DeviceNet 主站参数 |
| `dnm_rdnodetable` / `dnm_rdnodeinfo` | 读取节点表/信息 |
| `dnm_rderrorrecord` / `dnm_clrerrorrecord` | 读/清除错误记录 |
| `dnm_rdslvstatus` | 读取从站状态 |
| `dns_rdparam` / `dns_wrparam` | 读/写 DeviceNet 从站参数 |

#### 其他网络

| 函数名 | 说明 |
|--------|------|
| `flnt_rdparam` | FL-net 参数读取 |
| `cclr_rdparam` / `cclr_wrparam` | CC-Link IE 参数读/写 |
| `usb_rdinfo` / `usb_rdlog` | USB 信息/日志 |
| `ect_rdlog` / `ect_clrlog` | EtherCAT 日志 |
| `pnd_rdparam` / `pnd_wrparam` | 示教器参数 |
| `pnc_rdparam` / `pnc_wrparam` | 手持单元参数 |

### 8.13 程序再启动

| 函数名 | 说明 |
|--------|------|
| `cnc_rstrt_getpntcnt` | 获取再启动点数量 |
| `cnc_rstrt_rdpntlist` / `cnc_rstrt_rdpntlist2` | 读取再启动点列表 |
| `cnc_rstrt_rdpnt` / `cnc_rstrt_rdpnt2` | 读取再启动点 |
| `cnc_rstrt_rdmodal` | 读取模态数据 |
| `cnc_rstrt_selectpnt` | 选择再启动点 |
| `cnc_rstrt_wrpnt` / `cnc_rstrt_wrpnt2` | 写入再启动点 |
| `cnc_rstrt_createpnt` | 创建再启动点 |
| `cnc_rstrt_search` | 搜索再启动点 |
| `cnc_rstrt_getdncprg` | 获取 DNC 程序 |
| `cnc_rstrt_rdaddinfo` | 读取附加信息 |

### 8.14 IS-E 长行程（64 位）函数

以下函数为 IS-E 长行程系统的 64 位版本，使用 `REALDATA` 结构替代 `long` 类型：

| 函数名 | 说明 |
|--------|------|
| `cnc_rdaxisdata64` | 读取轴数据（64 位） |
| `cnc_prstwkcd64` | 预设工件坐标（64 位） |
| `cnc_wrrelpos64` | 写入相对位置（64 位） |
| `cnc_rdcommand64` | 读取命令值（64 位） |
| `cnc_rdparam64` / `cnc_wrparam64` | 读/写参数（64 位） |
| `cnc_zofs_rnge64` | 偏置范围（64 位） |
| `cnc_rdzofsr64` / `cnc_wrzofs64` | 读/写工件零点偏置（64 位） |
| `cnc_wksft_rnge64` | 工件移动范围（64 位） |
| `cnc_rdwkcdshft64` / `cnc_wrwkcdshft64` | 读/写工件坐标偏移（64 位） |
| `cnc_rdwkcdsfms64` / `cnc_wrwkcdsfms64` | 读/写工件坐标偏移测量（64 位） |
| `cnc_diagnoss64` / `cnc_diagnosr64` | 诊断数据（64 位） |
| `cnc_wrtofsdrctinp64` | 直接输入刀具偏置（64 位） |

### 8.15 MDD（宏表数据）

| 函数名 | 说明 |
|--------|------|
| `cnc_mdd_unlock` / `cnc_mdd_lock` | 解锁/锁定 MDD |
| `cnc_mdd_setpassword` | 设置密码 |
| `cnc_mdd_register` / `cnc_mdd_unregister` | 注册/注销 |
| `cnc_mdd_rdinfo` | 读取信息 |
| `cnc_mdd_setswitch` / `cnc_mdd_getswitch` | 设置/获取开关 |
| `cnc_mdd_update` | 更新 |

### 8.16 ROBO（机器人功能）

| 函数名 | 说明 |
|--------|------|
| `cnc_robo_rdsignals` / `cnc_robo_rdsignals2` | 读取信号 |
| `cnc_robo_rdalmmsg` | 读取报警消息 |
| `cnc_robo_rdgrouplist` / `cnc_robo_wrgroup` | 读/写组列表 |
| `cnc_robo_selectgroup` | 选择组 |
| `cnc_robo_wrsignalname` | 写入信号名 |
| `cnc_robo_rdcomsetting` / `cnc_robo_wrcomsetting` | 读/写通信设置 |
| `cnc_robo_rdponprop` | 读取属性 |

### 8.17 SRCS（串行伺服）

| 函数名 | 说明 |
|--------|------|
| `cnc_srcsrsvchnl` | 保留通道 |
| `cnc_srcsrdidinfo` / `cnc_srcswridinfo` | 读/写 ID 信息 |
| `cnc_srcsstartrd` / `cnc_srcsstartwrt` | 开始读/写 |
| `cnc_srcsstopexec` | 停止执行 |
| `cnc_srcsrdexstat` | 读取执行状态 |
| `cnc_srcsrdopdata` / `cnc_srcswropdata` | 读/写操作数据 |
| `cnc_srcsfreechnl` | 释放通道 |
| `cnc_srcsrdlayout` | 读取布局 |
| `cnc_srcsrddrvcp` | 读取驱动器容量 |

### 8.18 安全/RT 监控

| 函数名 | 说明 |
|--------|------|
| `cnc_get_mccteststs` | 获取 MC 测试状态 |
| `cnc_get_flowmonitor` | 获取流量监控 |
| `cnc_get_crosschk_alarm` | 获取交叉检查报警 |
| `cnc_get_safetysts` / `cnc_get_safetysts2` | 获取安全状态 |
| `cnc_getrtmrvars` | 获取 RT 监控变量 |
| `cnc_rdrtmrvars` / `cnc_wrrtmrvars` | 读/写 RT 监控变量 |
| `cnc_getrtmioinfo` | 获取 RT 监控 I/O 信息 |
| `cnc_rdrtmiowrenbl` / `cnc_wrrtmiowrenbl` | 读/写 RT 监控 I/O 使能 |

### 8.19 后台（BG）函数

以下函数可在 CNC 后台运行时安全调用：

| 函数名 | 说明 |
|--------|------|
| `cnc_absolute_bg` | 读取绝对位置（后台） |
| `cnc_relative_bg` | 读取相对位置（后台） |
| `cnc_machine_bg` | 读取机械位置（后台） |
| `cnc_statinfo_bg` | 读取机器状态（后台） |
| `cnc_rdseqnum_bg` | 读取序列号（后台） |
| `cnc_modal_bg` | 读取模态数据（后台） |
| `cnc_rdtofs_bg` | 读取刀具偏置（后台） |
| `cnc_rdzofs_bg` | 读取工件零点偏置（后台） |
| `cnc_rdalminfo_bg` | 读取报警信息（后台） |
| `cnc_rdexecprog_bg` | 读取执行程序（后台） |

---

## 附录：本项目常用 API 速查

以下为 `cnc_ops.c` 中实际使用的 API 函数：

| 用途 | API 函数 | 结构体 |
|------|----------|--------|
| 建立连接 | `cnc_allclibhndl3` | — |
| 断开连接 | `cnc_freelibhndl` | — |
| 设置超时 | `cnc_settimeout` | — |
| 系统信息 | `cnc_sysinfo` | — |
| 机器状态 | `cnc_statinfo` | — |
| 绝对位置 | `cnc_absolute` | `ODBAXIS` |
| 机械位置 | `cnc_machine` | `ODBAXIS` |
| 相对位置 | `cnc_relative` | `ODBAXIS` |
| 剩余移动量 | `cnc_distance` | `ODBAXIS` |
| 报警状态 | `cnc_alarm` | — |
| 报警消息 | `cnc_rdalmmsg` | — |
| 实际进给 | `cnc_actf` | `ODBACT` |
| 实际主轴速度 | `cnc_acts` | `ODBACT` |
| 程序号 | `cnc_rdprgnum` | `ODBPRO` |
| 执行程序名 | `cnc_exeprgname` | `ODBEXEPRG` |
| 序列号 | `cnc_rdseqnum` | `ODBSEQ` |
| 块计数 | `cnc_rdblkcount` | — |
| 全部动态数据 | `cnc_rddynamic2` | `ODBDY2` |
| 路径信息 | `cnc_getpath` | — |
| 宏变量 | `cnc_rdmacro` | `ODBM` |
| 刀具偏置 | `cnc_rdtofs` | `ODBTOFS` |
| 工件零点偏置 | `cnc_rdzofs` | `IODBZOFS` |
| 参数 | `cnc_rdparam` | `IODBPSD` |
| 设置 | `cnc_rdset` | `IODBPSD` |
| 错误码转文本 | `focas_error()` | 自定义 |

---

*本文档基于 fwlib32.h (Copyright 2003-2017 FANUC CORPORATION) 翻译整理，仅供开发参考。*
