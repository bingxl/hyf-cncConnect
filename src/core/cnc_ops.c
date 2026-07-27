#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "fwlib32.h"
#include "cnc_ops.h"

#define MAX_ALARM_SHOW  5
#define AXIS_NAMES      "XYZABCUVW"
#define DEFAULT_TIMEOUT 10

const char* focas_error(short ret)
{
    if (ret == EW_OK)       return "OK";
    if (ret == EW_BUSY)     return "Busy";
    if (ret == EW_RESET)    return "CNC reset";
    if (ret == EW_FUNC)     return "Function not supported";
    if (ret == EW_LENGTH)   return "Data block length error";
    if (ret == EW_NUMBER)   return "Data number error";
    if (ret == EW_RANGE)    return "Data range error";
    if (ret == EW_ATTRIB)   return "Data attribute error";
    if (ret == EW_DATA)     return "Data error";
    if (ret == EW_NOOPT)    return "No option";
    if (ret == EW_PROT)     return "Write protect error";
    if (ret == EW_OVRFLOW)  return "Memory overflow";
    if (ret == EW_PARAM)    return "Parameter error";
    if (ret == EW_BUFFER)   return "Buffer error";
    if (ret == EW_PATH)     return "Path error";
    if (ret == EW_MODE)     return "Mode error";
    if (ret == EW_REJECT)   return "Execution rejected";
    if (ret == EW_DTSRVR)   return "Data server error";
    if (ret == EW_ALARM)    return "Alarm occurred";
    if (ret == EW_STOP)     return "CNC not running";
    if (ret == EW_PASSWD)   return "Protection data error";
    if (ret == EW_PMC)      return "PMC error";
    if (ret == EW_SYSTEM)   return "System error";
    if (ret == EW_HANDLE)   return "Handle error";
    if (ret == EW_VERSION)  return "Version mismatch";
    if (ret == EW_UNEXP)    return "Abnormal error";
    if (ret == EW_SOCKET)   return "Socket error";
    if (ret == EW_NODLL)    return "DLL not found";
    if (ret == EW_BUS)      return "Bus error";
    if (ret == EW_HSSB)     return "HSSB error";
    return "Unknown error";
}

void print_line(const char *title)
{
    printf("\n--- %s ---\n", title);
}

int cnc_connect(const char *ip, int port, unsigned short *handle)
{
    short ret;
    printf("Connecting to %s:%d (timeout %ds) ...\n", ip, port, DEFAULT_TIMEOUT);
    ret = cnc_allclibhndl3(ip, (unsigned short)port, (long)DEFAULT_TIMEOUT, handle);
    if (ret != EW_OK) {
        printf("[ERROR] cnc_allclibhndl3: %s (code %d)\n", focas_error(ret), ret);
        return -1;
    }
    printf("Connected. handle = %u\n", *handle);
    return 0;
}

void cnc_disconnect(unsigned short handle)
{
    cnc_freelibhndl(handle);
    printf("Disconnected.\n");
}

int print_system_info(unsigned short handle)
{
    ODBSYS info;
    short ret;

    print_line("System Information");
    ret = cnc_sysinfo(handle, &info);
    if (ret != EW_OK) {
        printf("  cnc_sysinfo: %s (%d)\n", focas_error(ret), ret);
        return -1;
    }
    printf("  CNC Type:        %c%c\n", info.cnc_type[0], info.cnc_type[1]);
    printf("  MT Type:         %c%c\n", info.mt_type[0], info.mt_type[1]);
    printf("  Series:          %.4s\n", info.series);
    printf("  Version:         %.4s\n", info.version);
    printf("  Max Axes:        %d\n", (int)info.max_axis);
    printf("  Axes:            %.2s\n", info.axes);
    return 0;
}

int print_status(unsigned short handle)
{
    ODBST st;
    short ret;

    print_line("Machine Status");
    ret = cnc_statinfo(handle, &st);
    if (ret != EW_OK) {
        printf("  cnc_statinfo: %s (%d)\n", focas_error(ret), ret);
        return -1;
    }

    printf("  T/M Mode:        %d\n", (int)st.tmmode);
    printf("  Auto Mode:       %d\n", (int)st.aut);
    printf("  Run State:       %s\n", st.run ? "RUN" : "STOP");
    printf("  Motion:          %d\n", (int)st.motion);
    printf("  MSTB:            %d\n", (int)st.mstb);
    printf("  Emergency:       %s\n", st.emergency ? "YES" : "NO");
    printf("  Alarm:           %s\n", st.alarm ? "YES" : "NO");
    printf("  Edit Lock:       %s\n", st.edit ? "ON" : "OFF");
    return 0;
}

int print_positions(unsigned short handle)
{
    ODBAXIS pos;
    short ret;
    int i, count;
    const char *names = AXIS_NAMES;

    print_line("Axis Positions");

    ret = cnc_absolute(handle, 0, ALL_AXES, &pos);
    if (ret == EW_OK) {
        count = pos.type;
        printf("  Absolute:    ");
        for (i = 0; i < count && i < 9; i++)
            printf(" %c:%.4f", names[i], pos.data[i] / 10000.0);
        printf("\n");
    } else {
        printf("  Absolute:    [error %s]\n", focas_error(ret));
    }

    ret = cnc_machine(handle, 0, ALL_AXES, &pos);
    if (ret == EW_OK) {
        count = pos.type;
        printf("  Machine:     ");
        for (i = 0; i < count && i < 9; i++)
            printf(" %c:%.4f", names[i], pos.data[i] / 10000.0);
        printf("\n");
    } else {
        printf("  Machine:     [error %s]\n", focas_error(ret));
    }

    ret = cnc_relative(handle, 0, ALL_AXES, &pos);
    if (ret == EW_OK) {
        count = pos.type;
        printf("  Relative:    ");
        for (i = 0; i < count && i < 9; i++)
            printf(" %c:%.4f", names[i], pos.data[i] / 10000.0);
        printf("\n");
    } else {
        printf("  Relative:    [error %s]\n", focas_error(ret));
    }

    ret = cnc_distance(handle, 0, ALL_AXES, &pos);
    if (ret == EW_OK) {
        count = pos.type;
        printf("  Distance:    ");
        for (i = 0; i < count && i < 9; i++)
            printf(" %c:%.4f", names[i], pos.data[i] / 10000.0);
        printf("\n");
    } else {
        printf("  Distance:    [error %s]\n", focas_error(ret));
    }

    return 0;
}

int print_alarms(unsigned short handle)
{
    ODBALM alm;
    ODBALMMSG almmsg;
    short len;
    short ret;
    int i;

    print_line("Alarms");

    ret = cnc_alarm(handle, &alm);
    if (ret != EW_OK) {
        printf("  cnc_alarm: %s (%d)\n", focas_error(ret), ret);
    } else {
        printf("  Alarm Status:    %d", (int)alm.data);
        if (alm.data == 0)
            printf(" (none)");
        printf("\n");
    }

    printf("  Active Alarms:\n");
    for (i = 0; i < MAX_ALARM_SHOW; i++) {
        len = 0;
        memset(&almmsg, 0, sizeof(almmsg));
        ret = cnc_rdalmmsg(handle, (short)i, &len, &almmsg);
        if (ret != EW_OK)
            break;
        if (almmsg.alm_no == 0)
            break;
        printf("    #%ld [axis %c]: %s\n",
               almmsg.alm_no, almmsg.axis, almmsg.alm_msg);
    }
    if (i == 0)
        printf("    (none)\n");

    return 0;
}

int print_actf_acts(unsigned short handle)
{
    ODBACT feed, spindle;
    short ret;

    print_line("Actual Feed / Spindle");

    ret = cnc_actf(handle, &feed);
    if (ret == EW_OK)
        printf("  Actual Feedrate:  %ld\n", feed.data);

    ret = cnc_acts(handle, &spindle);
    if (ret == EW_OK)
        printf("  Actual Spindle:   %ld\n", spindle.data);

    return 0;
}

int print_program_info(unsigned short handle)
{
    ODBPRO prg;
    ODBEXEPRG exeprg;
    ODBSEQ seq;
    long blk;
    short ret;

    print_line("Program Info");

    ret = cnc_rdprgnum(handle, &prg);
    if (ret == EW_OK)
        printf("  Program Number:  %d (main: %d)\n", (int)prg.data, (int)prg.mdata);

    memset(&exeprg, 0, sizeof(exeprg));
    ret = cnc_exeprgname(handle, &exeprg);
    if (ret == EW_OK)
        printf("  Program Name:    %s\n", exeprg.name);

    ret = cnc_rdseqnum(handle, &seq);
    if (ret == EW_OK)
        printf("  Sequence Number: %ld\n", seq.data);

    ret = cnc_rdblkcount(handle, &blk);
    if (ret == EW_OK)
        printf("  Block Count:     %ld\n", blk);

    return 0;
}

int print_dynamic(unsigned short handle)
{
    ODBDY2 dyn;
    short ret;

    print_line("Dynamic Data");
    ret = cnc_rddynamic2(handle, 0, sizeof(dyn), &dyn);
    if (ret != EW_OK) {
        printf("  cnc_rddynamic2: %s (%d)\n", focas_error(ret), ret);
        return -1;
    }
    printf("  Feedrate:        %ld\n", dyn.actf);
    printf("  Spindle Speed:   %ld\n", dyn.acts);
    printf("  Alarm Status:    %ld\n", dyn.alarm);
    printf("  Program No:      %ld\n", dyn.prgnum);
    printf("  Main Program:    %ld\n", dyn.prgmnum);
    printf("  Sequence No:     %ld\n", dyn.seqnum);
    printf("  Axes:            %d\n", (int)dyn.axis);
    return 0;
}

int print_program_list(unsigned short handle)
{
    PRGDIR2 dir[100];
    short ret, i;
    long top = 0;
    short count = 100;

    print_line("Program File List");

    ret = cnc_rdprogdir2(handle, 2, &top, &count, dir);
    if (ret != EW_OK) {
        printf("  cnc_rdprogdir2: %s (%d)\n", focas_error(ret), ret);
        return -1;
    }

    printf("  Total programs: %d\n\n", (int)count);
    for (i = 0; i < count; i++)
        printf("  O%04ld  (%ld bytes)  %s\n",
               dir[i].number, dir[i].length, dir[i].comment);

    return 0;
}

int print_program_content(unsigned short handle, long prog_no)
{
    ODBUP up;
    unsigned short length;
    short ret;
    int empty_count = 0;

    printf("\n--- Program Content - O%04ld ---\n", prog_no);

    ret = cnc_upstart(handle, prog_no);
    if (ret != EW_OK) {
        printf("  cnc_upstart: %s (%d)\n", focas_error(ret), ret);
        return -1;
    }

    for (;;) {
        length = sizeof(up.data);
        ret = cnc_upload(handle, &up, &length);
        if (ret == EW_RESET)
            break;
        if (ret != EW_OK) {
            printf("  cnc_upload: %s (%d)\n", focas_error(ret), ret);
            cnc_upend(handle);
            return -1;
        }
        if (length == 0) {
            if (++empty_count > 3)
                break;
            continue;
        }
        empty_count = 0;
        up.data[length] = '\0';
        printf("%s", up.data);
    }

    cnc_upend(handle);
    return 0;
}

int print_path_info(unsigned short handle)
{
    short path, paths;
    short ret;

    print_line("Path Info");
    ret = cnc_getpath(handle, &path, &paths);
    if (ret != EW_OK) {
        printf("  cnc_getpath: %s (%d)\n", focas_error(ret), ret);
        return -1;
    }
    printf("  Current Path:    %d / %d\n", path + 1, paths);
    return 0;
}

int print_macro_variables(unsigned short handle)
{
    ODBM macro;
    short ret;
    int i;

    print_line("Macro Variables (#1 - #10)");
    for (i = 1; i <= 10; i++) {
        memset(&macro, 0, sizeof(macro));
        ret = cnc_rdmacro(handle, (short)i, sizeof(macro), &macro);
        if (ret != EW_OK)
            continue;
        printf("  #%-3d = %12.4f\n", i, macro.mcr_val / 10000.0);
    }
    return 0;
}

int print_tool_offsets(unsigned short handle)
{
    ODBTOFS ofs;
    short ret;
    int i;

    print_line("Tool Offsets (#0 - #9, type 0)");
    for (i = 0; i <= 9; i++) {
        memset(&ofs, 0, sizeof(ofs));
        ret = cnc_rdtofs(handle, (short)i, 0, sizeof(ofs), &ofs);
        if (ret != EW_OK)
            continue;
        printf("  Tool Offset #%d = %12.4f\n", i, ofs.data / 10000.0);
    }
    return 0;
}

int print_work_zero_offsets(unsigned short handle)
{
    IODBZOFS zofs;
    short ret;
    int i, count;
    const char *names = AXIS_NAMES;

    print_line("Work Zero Offsets (#0)");
    memset(&zofs, 0, sizeof(zofs));
    ret = cnc_rdzofs(handle, 0, ALL_AXES, sizeof(zofs), &zofs);
    if (ret != EW_OK) {
        printf("  cnc_rdzofs: %s (%d)\n", focas_error(ret), ret);
        return -1;
    }
    count = zofs.type;
    for (i = 0; i < count && i < 9; i++)
        printf("  %c: %12.4f\n", names[i], zofs.data[i] / 10000.0);
    return 0;
}

int print_parameters(unsigned short handle)
{
    IODBPSD param;
    short ret;

    print_line("Key Parameters");

    memset(&param, 0, sizeof(param));
    ret = cnc_rdparam(handle, 6750, 0, sizeof(param), &param);
    if (ret == EW_OK)
        printf("  #6750 (Rapid Traverse): %ld\n", param.u.ldata);

    memset(&param, 0, sizeof(param));
    ret = cnc_rdset(handle, 0, 0, sizeof(param), &param);
    if (ret == EW_OK)
        printf("  Setting #0:             %ld\n", param.u.ldata);

    return 0;
}

static long scale(long val, short dec)
{
    while (dec-- > 0)
        val /= 10;
    return val;
}

int read_counts(unsigned short handle, PartCount *pc)
{
    IODBPSD param;
    ODBM m;
    short ret;

    memset(&m, 0, sizeof(m));
    ret = cnc_rdmacro(handle, 3901, sizeof(m), &m);
    if (ret == EW_OK)
        pc->current = scale(m.mcr_val, m.dec_val);

    memset(&m, 0, sizeof(m));
    ret = cnc_rdmacro(handle, 3902, sizeof(m), &m);
    if (ret == EW_OK)
        pc->required = scale(m.mcr_val, m.dec_val);

    memset(&param, 0, sizeof(param));
    ret = cnc_rdparam(handle, 6712, 0, sizeof(param), &param);
    if (ret == EW_OK)
        pc->total = param.u.ldata;

    return 0;
}

int get_part_count(unsigned short handle, PartCount *pc)
{
    pc->current = -1;
    pc->required = -1;
    pc->total = -1;
    return read_counts(handle, pc);
}

int get_part_count_on_path(unsigned short handle, short path, PartCount *pc)
{
    short prev;

    pc->current = -1;
    pc->required = -1;
    pc->total = -1;

    { short path_cnt; cnc_getpath(handle, &prev, &path_cnt); }
    cnc_setpath(handle, path);
    read_counts(handle, pc);
    cnc_setpath(handle, prev);

    return 0;
}

int monitor_loop(unsigned short handle, int interval_ms,
                  short parts_cur_var, short parts_total_var)
{
    ODBST st;
    ODBAXIS pos;
    const char *names = "XYZABCUVW";
    short ret;
    int i, count;

    printf("\nMonitoring (interval %dms). Press Ctrl+C to stop.\n\n", interval_ms);

    while (1) {
        ret = cnc_statinfo(handle, &st);
        if (ret != EW_OK) {
            printf("[ERROR] statinfo: %s\n", focas_error(ret));
            Sleep(interval_ms);
            continue;
        }

        ret = cnc_absolute(handle, 0, ALL_AXES, &pos);
        count = (ret == EW_OK) ? pos.type : 0;

        printf("[%s] %s%s",
               st.run ? "RUN " : "STOP",
               st.alarm ? "ALARM " : "OK",
               st.emergency ? " E-STOP" : "");

        for (i = 0; i < count && i < 4; i++)
            printf("  %c:%.3f", names[i], pos.data[i] / 10000.0);

        if (parts_cur_var > 0) {
            ODBM m;
            memset(&m, 0, sizeof(m));
            if (cnc_rdmacro(handle, parts_cur_var, sizeof(m), &m) == EW_OK)
                printf("  Parts:%ld", m.mcr_val);
            memset(&m, 0, sizeof(m));
            if (cnc_rdmacro(handle, parts_total_var, sizeof(m), &m) == EW_OK)
                printf("/%ld", m.mcr_val);
        }
        printf("\n");

        Sleep(interval_ms);
    }

    return 0;
}

int fetch_system_info(unsigned short handle, CncSystemInfo *info)
{
    ODBSYS raw;
    short ret;
    memset(info, 0, sizeof(*info));
    ret = cnc_sysinfo(handle, &raw);
    if (ret != EW_OK) return -1;
    info->cnc_type[0] = raw.cnc_type[0];
    info->cnc_type[1] = raw.cnc_type[1];
    info->cnc_type[2] = 0;
    info->mt_type[0] = raw.mt_type[0];
    info->mt_type[1] = raw.mt_type[1];
    info->mt_type[2] = 0;
    memcpy(info->series, raw.series, 4);
    info->series[4] = 0;
    memcpy(info->version, raw.version, 4);
    info->version[4] = 0;
    info->max_axis = (int)raw.max_axis;
    memcpy(info->axes, raw.axes, 2);
    info->axes[2] = 0;
    return 0;
}

int fetch_status(unsigned short handle, CncStatus *status)
{
    ODBST st;
    short ret;
    memset(status, 0, sizeof(*status));
    ret = cnc_statinfo(handle, &st);
    if (ret != EW_OK) return -1;
    status->tmmode = (int)st.tmmode;
    status->aut = (int)st.aut;
    status->run = (int)st.run;
    status->motion = (int)st.motion;
    status->mstb = (int)st.mstb;
    status->emergency = (int)st.emergency;
    status->alarm = (int)st.alarm;
    status->edit = (int)st.edit;
    return 0;
}

int fetch_positions(unsigned short handle, CncPositions *pos)
{
    ODBAXIS raw;
    short ret;
    int i, count;
    const char *names = AXIS_NAMES;
    memset(pos, 0, sizeof(*pos));

    ret = cnc_absolute(handle, 0, ALL_AXES, &raw);
    if (ret == EW_OK) {
        count = raw.type;
        pos->count = count;
        for (i = 0; i < count && i < CNC_MAX_AXES; i++)
            pos->absolute[i] = raw.data[i] / 10000.0;
    }

    ret = cnc_machine(handle, 0, ALL_AXES, &raw);
    if (ret == EW_OK) {
        count = raw.type;
        if (pos->count == 0) pos->count = count;
        for (i = 0; i < count && i < CNC_MAX_AXES; i++)
            pos->machine[i] = raw.data[i] / 10000.0;
    }

    ret = cnc_relative(handle, 0, ALL_AXES, &raw);
    if (ret == EW_OK) {
        count = raw.type;
        for (i = 0; i < count && i < CNC_MAX_AXES; i++)
            pos->relative[i] = raw.data[i] / 10000.0;
    }

    ret = cnc_distance(handle, 0, ALL_AXES, &raw);
    if (ret == EW_OK) {
        count = raw.type;
        for (i = 0; i < count && i < CNC_MAX_AXES; i++)
            pos->distance[i] = raw.data[i] / 10000.0;
    }

    if (pos->count > CNC_MAX_AXES) pos->count = CNC_MAX_AXES;
    return 0;
}

int fetch_alarms(unsigned short handle, CncAlarms *alarms)
{
    ODBALM alm;
    ODBALMMSG almmsg;
    short len;
    short ret;
    int i;
    memset(alarms, 0, sizeof(*alarms));

    for (i = 0; i < CNC_MAX_ALARMS; i++) {
        len = 0;
        memset(&almmsg, 0, sizeof(almmsg));
        ret = cnc_rdalmmsg(handle, (short)i, &len, &almmsg);
        if (ret != EW_OK) break;
        if (almmsg.alm_no == 0) break;
        alarms->alarm_no[i] = almmsg.alm_no;
        alarms->axis[i] = almmsg.axis;
        memcpy(alarms->msg[i], almmsg.alm_msg, 31);
        alarms->msg[i][31] = 0;
    }
    alarms->count = i;
    return 0;
}

int fetch_act_data(unsigned short handle, CncActData *act)
{
    ODBACT feed, spindle;
    short ret;
    memset(act, 0, sizeof(*act));
    ret = cnc_actf(handle, &feed);
    if (ret == EW_OK) act->feedrate = feed.data;
    ret = cnc_acts(handle, &spindle);
    if (ret == EW_OK) act->spindle = spindle.data;
    return 0;
}

int fetch_program_info(unsigned short handle, CncProgramInfo *prog)
{
    ODBPRO prg;
    ODBEXEPRG exeprg;
    ODBSEQ seq;
    long blk;
    short ret;
    memset(prog, 0, sizeof(*prog));
    ret = cnc_rdprgnum(handle, &prg);
    if (ret == EW_OK) {
        prog->prg_number = (int)prg.data;
        prog->prg_main = (int)prg.mdata;
    }
    memset(&exeprg, 0, sizeof(exeprg));
    ret = cnc_exeprgname(handle, &exeprg);
    if (ret == EW_OK) memcpy(prog->prg_name, exeprg.name, 35);
    prog->prg_name[35] = 0;
    ret = cnc_rdseqnum(handle, &seq);
    if (ret == EW_OK) prog->seq_number = seq.data;
    ret = cnc_rdblkcount(handle, &blk);
    if (ret == EW_OK) prog->blk_count = blk;
    return 0;
}

int fetch_dynamic(unsigned short handle, CncDynamicData *dyn)
{
    ODBDY2 raw;
    short ret;
    memset(dyn, 0, sizeof(*dyn));
    ret = cnc_rddynamic2(handle, 0, sizeof(raw), &raw);
    if (ret != EW_OK) return -1;
    dyn->actf = raw.actf;
    dyn->acts = raw.acts;
    dyn->alarm = raw.alarm;
    dyn->prgnum = raw.prgnum;
    dyn->prgmnum = raw.prgmnum;
    dyn->seqnum = raw.seqnum;
    dyn->axis = (int)raw.axis;
    return 0;
}

int fetch_program_list(unsigned short handle, CncProgramList *list)
{
    PRGDIR2 dir[CNC_MAX_PROGRAMS];
    short ret, i;
    long top = 0;
    short count = CNC_MAX_PROGRAMS;
    memset(list, 0, sizeof(*list));
    ret = cnc_rdprogdir2(handle, 2, &top, &count, dir);
    if (ret != EW_OK) return -1;
    if (count > CNC_MAX_PROGRAMS) count = CNC_MAX_PROGRAMS;
    list->count = count;
    for (i = 0; i < count && i < CNC_MAX_PROGRAMS; i++) {
        list->number[i] = dir[i].number;
        list->length[i] = dir[i].length;
        memcpy(list->comment[i], dir[i].comment, 35);
        list->comment[i][35] = 0;
    }
    return 0;
}

int fetch_macro_vars(unsigned short handle, CncMacroData *data)
{
    ODBM macro;
    short ret;
    int i;
    memset(data, 0, sizeof(*data));
    for (i = 1; i <= CNC_MAX_MACRO; i++) {
        memset(&macro, 0, sizeof(macro));
        ret = cnc_rdmacro(handle, (short)i, sizeof(macro), &macro);
        if (ret != EW_OK) continue;
        data->values[data->count++] = macro.mcr_val / 10000.0;
    }
    return data->count;
}

int fetch_tool_offsets(unsigned short handle, CncToolOffsetData *data)
{
    ODBTOFS ofs;
    short ret;
    int i;
    memset(data, 0, sizeof(*data));
    for (i = 0; i < CNC_MAX_TOOL; i++) {
        memset(&ofs, 0, sizeof(ofs));
        ret = cnc_rdtofs(handle, (short)i, 0, sizeof(ofs), &ofs);
        if (ret != EW_OK) continue;
        data->values[data->count++] = ofs.data / 10000.0;
    }
    return data->count;
}

int fetch_work_zero(unsigned short handle, CncWorkZeroData *data)
{
    IODBZOFS zofs;
    short ret;
    int i;
    memset(data, 0, sizeof(*data));
    memset(&zofs, 0, sizeof(zofs));
    ret = cnc_rdzofs(handle, 0, ALL_AXES, sizeof(zofs), &zofs);
    if (ret != EW_OK) return -1;
    data->count = zofs.type;
    if (data->count > CNC_MAX_AXES) data->count = CNC_MAX_AXES;
    for (i = 0; i < data->count; i++)
        data->values[i] = zofs.data[i] / 10000.0;
    return data->count;
}

int fetch_parameters(unsigned short handle, long *param_6750, long *setting_0)
{
    IODBPSD param;
    short ret;
    *param_6750 = 0;
    *setting_0 = 0;
    memset(&param, 0, sizeof(param));
    ret = cnc_rdparam(handle, 6750, 0, sizeof(param), &param);
    if (ret == EW_OK) *param_6750 = param.u.ldata;
    memset(&param, 0, sizeof(param));
    ret = cnc_rdset(handle, 0, 0, sizeof(param), &param);
    if (ret == EW_OK) *setting_0 = param.u.ldata;
    return 0;
}

int fetch_path_info(unsigned short handle, short *current, short *count)
{
    short data_cnt;
    int ret = cnc_getpath(handle, current, &data_cnt);
    if (ret == EW_OK) *count = data_cnt;
    return ret;
}

int fetch_machine_data(const char *ip, int port, CncMachineData *data)
{
    unsigned short handle;
    memset(data, 0, sizeof(*data));
    data->ok = 0;

    if (cnc_connect(ip, port, &handle) != 0) {
        _snprintf(data->error_msg, sizeof(data->error_msg),
                  "连接失败: %s:%d", ip, port);
        return -1;
    }

    cnc_settimeout(handle, 5);

    fetch_system_info(handle, &data->sys);
    fetch_status(handle, &data->status);
    fetch_positions(handle, &data->pos);
    fetch_alarms(handle, &data->alarms);
    fetch_act_data(handle, &data->act);
    fetch_program_info(handle, &data->prog);
    fetch_dynamic(handle, &data->dyn);
    get_part_count(handle, &data->part_count);
    fetch_program_list(handle, &data->prog_list);
    fetch_macro_vars(handle, &data->macro_vars);
    fetch_tool_offsets(handle, &data->tool_offsets);
    fetch_work_zero(handle, &data->work_zero);
    fetch_parameters(handle, &data->param_6750, &data->setting_0);
    fetch_path_info(handle, &data->path_current, &data->path_count);

    cnc_disconnect(handle);
    data->ok = 1;
    if (data->alarms.count < 0) data->alarms.count = 0;
    return 0;
}
