/*
 * mazak_adapter.cpp - MTConnect adapter for MAZAK machines.
 *
 * MAZAK exposes MTConnect in "pull" mode: the client sends commands
 * ("MTConnect Probe" / "MTConnect Streams") over TCP and receives
 * "tag|value" lines. This adapter pulls that data and re-emits it as
 * standard SHDR ("push") for the local MTConnect agent, so MAZAK
 * integrates into the same pipeline as FANUC.
 *
 * Usage: mazak_adapter.exe run <mazak-ip> <mtconnect-port> [shdr-port]
 */

#include "internal.hpp"
#include "adapter.hpp"
#include "device_datum.hpp"
#include "server.hpp"
#include "service.hpp"

#define MAX_MZ_AXES 3
#define MZ_BUF_SIZE (64 * 1024)

class MazakAdapter : public Adapter, public MTConnectService
{
protected:
  /* device data (keys must match devices/mazak.xml) */
  Availability  mAvail;
  EmergencyStop mEstop;
  Execution     mExecution;
  ControllerMode mMode;
  Event         mProgram;
  Event         mProgramInfo;
  Event         mBlock;
  IntEvent      mLine;
  Sample        mPathFeedrate;
  PathPosition  mPathPosition;
  Sample        mPartTotal;
  Sample        mPartCurrent;
  Sample        mPartRequired;
  Condition     mAlarm;
  Condition     mSystem;
  Sample       *mAxisAct[MAX_MZ_AXES];
  Sample       *mAxisLoad[MAX_MZ_AXES];
  Sample        mSspeed;
  Sample        mSload;

  /* connection */
  SOCKET mSock;
  int mDevicePort;
  const char *mDeviceIP;
  bool mConnected;

  void connect();
  void disconnect();
  void endRound();
  void getData();
  void mapTag(const char *tag, const char *val);

public:
  MazakAdapter(int aPort);
  ~MazakAdapter();

  virtual void initialize(int aArgc, const char *aArgv[]);
  virtual void start();
  virtual void stop();
  virtual void gatherDeviceData();
};

MazakAdapter::MazakAdapter(int aPort)
  : Adapter(aPort, 100),
    mAvail("avail"), mEstop("estop"), mExecution("execution"), mMode("mode"),
    mProgram("program"), mProgramInfo("programInfo"), mBlock("block"),
    mLine("line"), mPathFeedrate("pathFeedrate"), mPathPosition("pathPosition"),
    mPartTotal("part_total"), mPartCurrent("part_current"),
    mPartRequired("part_required"), mAlarm("alarm"), mSystem("system"),
    mSspeed("Sspeed"), mSload("Sload")
{
  addDatum(mAvail);
  addDatum(mEstop);
  addDatum(mExecution);
  addDatum(mMode);
  addDatum(mProgram);
  addDatum(mProgramInfo);
  addDatum(mBlock);
  addDatum(mLine);
  addDatum(mPathFeedrate);
  addDatum(mPathPosition);
  addDatum(mPartTotal);
  addDatum(mPartCurrent);
  addDatum(mPartRequired);
  addDatum(mAlarm);
  addDatum(mSystem);
  addDatum(mSspeed);
  addDatum(mSload);

  const char *axes[MAX_MZ_AXES] = { "X", "Y", "Z" };
  for (int i = 0; i < MAX_MZ_AXES; i++) {
    char act[16], load[16];
    sprintf(act, "%sact", axes[i]);
    sprintf(load, "%sload", axes[i]);
    mAxisAct[i] = new Sample(act);
    mAxisLoad[i] = new Sample(load);
    addDatum(*mAxisAct[i]);
    addDatum(*mAxisLoad[i]);
  }

  mConnected = false;
  mSock = INVALID_SOCKET;
  mDeviceIP = NULL;
  mDevicePort = 7878;
  mAvail.unavailable();
}

MazakAdapter::~MazakAdapter()
{
  for (int i = 0; i < MAX_MZ_AXES; i++) {
    delete mAxisAct[i];
    delete mAxisLoad[i];
  }
  disconnect();
}

void MazakAdapter::initialize(int aArgc, const char *aArgv[])
{
  MTConnectService::initialize(aArgc, aArgv);
  mDeviceIP = aArgv[0];
  mDevicePort = atoi(aArgv[1]);

  if (aArgc >= 3)
    mPort = atoi(aArgv[2]);
}

void MazakAdapter::start()
{
  startServer();
}

void MazakAdapter::stop()
{
  stopServer();
}
/* ---------- connection (pull mode) ---------- */
void MazakAdapter::connect()
{
  if (mConnected) return;

  struct sockaddr_in addr;
  mSock = socket(AF_INET, SOCK_STREAM, 0);
  if (mSock == INVALID_SOCKET) { mAvail.unavailable(); return; }

  addr.sin_family = AF_INET;
  addr.sin_port = htons((u_short)mDevicePort);
  addr.sin_addr.s_addr = inet_addr(mDeviceIP);
  if (addr.sin_addr.s_addr == INADDR_NONE) {
    struct hostent *he = gethostbyname(mDeviceIP);
    if (!he) { closesocket(mSock); mSock = INVALID_SOCKET; mAvail.unavailable(); return; }
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
  }

  /* non-blocking connect with timeout */
  u_long mode = 1;
  ioctlsocket(mSock, FIONBIO, &mode);
  if (::connect(mSock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    fd_set wfds;
    struct timeval tv = { 10, 0 };
    FD_ZERO(&wfds);
    FD_SET(mSock, &wfds);
    if (select(0, NULL, &wfds, NULL, &tv) <= 0) {
      closesocket(mSock);
      mSock = INVALID_SOCKET;
      mAvail.unavailable();
      return;
    }
  }
  mode = 0;
  ioctlsocket(mSock, FIONBIO, &mode);

  mConnected = true;
  mAvail.available();
  printf("Mazak connected to %s:%d\n", mDeviceIP, mDevicePort);
}

void MazakAdapter::disconnect()
{
  if (mSock != INVALID_SOCKET) {
    closesocket(mSock);
    mSock = INVALID_SOCKET;
  }
  if (mConnected) {
    mConnected = false;
    mAvail.unavailable();
    printf("Mazak disconnected\n");
  }
}

/* close only the socket after a successful short pull; keep AVAILABLE so
   the agent retains the last values between poll rounds */
void MazakAdapter::endRound()
{
  if (mSock != INVALID_SOCKET) {
    closesocket(mSock);
    mSock = INVALID_SOCKET;
  }
  mConnected = false;
}

/* ---------- data pull + mapping ---------- */
static int looks_like_ts(const char *s)
{
  /* MTConnect timestamps look like 2024-01-01T12:00:00.000Z */
  if (strlen(s) < 20) return 0;
  for (int i = 0; i < 10; i++)
    if (i == 4 || i == 7) { if (s[i] != '-') return 0; }
    else if (!isdigit((unsigned char)s[i])) return 0;
  return 1;
}
void MazakAdapter::mapTag(const char *tag, const char *val)
{
  const char *v = val;

  if (strcmp(tag, "avail") == 0 || strcmp(tag, "availability") == 0) {
    if (strcmp(v, "UNAVAILABLE") == 0) mAvail.unavailable();
    else mAvail.available();
    return;
  }
  if (strcmp(tag, "estop") == 0) {
    mEstop.setValue(strcmp(v, "ARMED") == 0 ? EmergencyStop::eARMED : EmergencyStop::eTRIGGERED);
    return;
  }
  if (strcmp(tag, "execution") == 0) {
    if (strcmp(v, "ACTIVE") == 0) mExecution.setValue(Execution::eACTIVE);
    else if (strcmp(v, "INTERRUPTED") == 0) mExecution.setValue(Execution::eINTERRUPTED);
    else if (strcmp(v, "STOPPED") == 0) mExecution.setValue(Execution::eSTOPPED);
    else mExecution.setValue(Execution::eREADY);
    return;
  }
  if (strcmp(tag, "mode") == 0 || strcmp(tag, "controllerMode") == 0) {
    if (strcmp(v, "AUTOMATIC") == 0) mMode.setValue(ControllerMode::eAUTOMATIC);
    else if (strcmp(v, "MANUAL") == 0) mMode.setValue(ControllerMode::eMANUAL);
    else if (strcmp(v, "MANUAL_DATA_INPUT") == 0) mMode.setValue(ControllerMode::eMANUAL_DATA_INPUT);
    else mMode.setValue(ControllerMode::eSEMI_AUTOMATIC);
    return;
  }
  if (strcmp(tag, "program") == 0) { mProgram.setValue(v); return; }
  if (strcmp(tag, "programInfo") == 0 || strcmp(tag, "program_comment") == 0 ||
      strcmp(tag, "comment") == 0) { mProgramInfo.setValue(v); return; }
  if (strcmp(tag, "line") == 0) { mLine.setValue(atoi(v)); return; }
  if (strcmp(tag, "block") == 0) { mBlock.setValue(v); return; }
  if (strcmp(tag, "pathFeedrate") == 0 || strcmp(tag, "feedrate") == 0)
    { mPathFeedrate.setValue(atof(v)); return; }
  if (strcmp(tag, "pathPosition") == 0) {
    double x, y, z;
    if (sscanf(v, "%lf %lf %lf", &x, &y, &z) == 3)
      mPathPosition.setValue(x, y, z);
    return;
  }
  if (strcmp(tag, "part_total") == 0 || strcmp(tag, "partTotal") == 0)
    { mPartTotal.setValue(atof(v)); return; }
  if (strcmp(tag, "part_current") == 0 || strcmp(tag, "partCurrent") == 0)
    { mPartCurrent.setValue(atof(v)); return; }
  if (strcmp(tag, "part_required") == 0 || strcmp(tag, "partRequired") == 0)
    { mPartRequired.setValue(atof(v)); return; }
  if (strcmp(tag, "Xact") == 0 || strcmp(tag, "xPosition") == 0)
    { mAxisAct[0]->setValue(atof(v)); return; }
  if (strcmp(tag, "Xload") == 0) { mAxisLoad[0]->setValue(atof(v)); return; }
  if (strcmp(tag, "Yact") == 0 || strcmp(tag, "yPosition") == 0)
    { mAxisAct[1]->setValue(atof(v)); return; }
  if (strcmp(tag, "Yload") == 0) { mAxisLoad[1]->setValue(atof(v)); return; }
  if (strcmp(tag, "Zact") == 0 || strcmp(tag, "zPosition") == 0)
    { mAxisAct[2]->setValue(atof(v)); return; }
  if (strcmp(tag, "Zload") == 0) { mAxisLoad[2]->setValue(atof(v)); return; }
  if (strcmp(tag, "Sspeed") == 0 || strcmp(tag, "spindleSpeed") == 0)
    { mSspeed.setValue(atof(v)); return; }
  if (strcmp(tag, "Sload") == 0) { mSload.setValue(atof(v)); return; }
  if (strcmp(tag, "alarm") == 0) {
    mAlarm.setValue(strcmp(v, "NORMAL") == 0 ? Condition::eNORMAL : Condition::eFAULT, v, "", "", "");
    return;
  }
  if (strcmp(tag, "system") == 0) {
    mSystem.setValue(strcmp(v, "NORMAL") == 0 ? Condition::eNORMAL : Condition::eFAULT, v, "", "", "");
    return;
  }
  /* unknown tag: ignore silently (extend mazak.xml + mapTag as needed) */
}
void MazakAdapter::getData()
{
  char cmd[] = "MTConnect Streams\n";
  char buf[MZ_BUF_SIZE];
  int n;

  if (!mConnected) return;

  /* recv timeout so a stalled Mazak reply does not block the loop */
  DWORD rcv_tmo = 3000;
  setsockopt(mSock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&rcv_tmo, sizeof(rcv_tmo));

  if (send(mSock, cmd, (int)strlen(cmd), 0) == SOCKET_ERROR) {
    printf("Mazak send err %d\n", WSAGetLastError()); disconnect(); return;
  }

  n = recv(mSock, buf, sizeof(buf) - 1, 0);
  if (n < 0 && WSAGetLastError() == WSAETIMEDOUT)
    return;                       /* slow Mazak: skip this round */
  if (n <= 0) {
    printf("Mazak recv n=%d err=%d\n", n, WSAGetLastError()); disconnect(); return;
  }
  buf[n] = '\0';

  /* each line:  [timestamp|]tag|value   (also tolerate ts|key|value) */
  char *line = strtok(buf, "\r\n");
  while (line) {
    char *s = line;
    /* skip leading timestamp segment if present */
    if (strchr(s, '|')) {
      char *first = s;
      char *sep = strchr(first, '|');
      if (sep) {
        *sep = '\0';
        if (!looks_like_ts(first)) {
          /* not a timestamp; first segment is the tag, restore and re-split */
          *sep = '|';
          sep = strchr(s, '|');
        }
        if (sep) {
          char *tag = s;
          char *val = sep + 1;
          *sep = '\0';
          mapTag(tag, val);
        }
      }
    }
    line = strtok(NULL, "\r\n");
  }
}

void MazakAdapter::gatherDeviceData()
{
  /* MAZAK MTConnect is pull mode: fresh connection per round. */
  connect();
  if (mConnected) {
    getData();
    endRound();   /* close socket, keep AVAILABLE */
  }
}

/* ---------- entry point ---------- */
int main(int aArgc, const char *aArgv[])
{
  /* argv: [exe] run <ip> <mtconnect-port> [shdr-port] */
  int shdrPort = 7878;
  if (aArgc >= 5)
    shdrPort = atoi(aArgv[4]);

  MazakAdapter *adapter = new MazakAdapter(shdrPort);
  adapter->setName("MTConnect Mazak Adapter");
  return adapter->main(aArgc, aArgv);
}
