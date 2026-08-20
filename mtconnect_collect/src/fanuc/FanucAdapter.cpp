/*
* Copyright (c) 2008, AMT – The Association For Manufacturing Technology (“AMT”)
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name of the AMT nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* DISCLAIMER OF WARRANTY. ALL MTCONNECT MATERIALS AND SPECIFICATIONS PROVIDED
* BY AMT, MTCONNECT OR ANY PARTICIPANT TO YOU OR ANY PARTY ARE PROVIDED "AS IS"
* AND WITHOUT ANY WARRANTY OF ANY KIND. AMT, MTCONNECT, AND EACH OF THEIR
* RESPECTIVE MEMBERS, OFFICERS, DIRECTORS, AFFILIATES, SPONSORS, AND AGENTS
* (COLLECTIVELY, THE "AMT PARTIES") AND PARTICIPANTS MAKE NO REPRESENTATION OR
* WARRANTY OF ANY KIND WHATSOEVER RELATING TO THESE MATERIALS, INCLUDING, WITHOUT
* LIMITATION, ANY EXPRESS OR IMPLIED WARRANTY OF NONINFRINGEMENT,
* MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. 

* LIMITATION OF LIABILITY. IN NO EVENT SHALL AMT, MTCONNECT, ANY OTHER AMT
* PARTY, OR ANY PARTICIPANT BE LIABLE FOR THE COST OF PROCURING SUBSTITUTE GOODS
* OR SERVICES, LOST PROFITS, LOSS OF USE, LOSS OF DATA OR ANY INCIDENTAL,
* CONSEQUENTIAL, INDIRECT, SPECIAL OR PUNITIVE DAMAGES OR OTHER DIRECT DAMAGES,
* WHETHER UNDER CONTRACT, TORT, WARRANTY OR OTHERWISE, ARISING IN ANY WAY OUT OF
* THIS AGREEMENT, USE OR INABILITY TO USE MTCONNECT MATERIALS, WHETHER OR NOT
* SUCH PARTY HAD ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES.
*/

#include "internal.hpp"
#include "fanuc_adapter.hpp"
#include "server.hpp"
#include "string_buffer.hpp"
#include "config.hpp"

int main(int aArgc, const char *aArgv[])
{
  /*
   * Usage: fanuc_adapter.exe run <machine-ip> <focas-port> [shdr-port]
   *
   * Example: fanuc_adapter.exe run 192.168.11.186 8193 7878
   */
  int shdrPort = 7878;
  if (aArgc >= 5)
    shdrPort = atoi(aArgv[4]);

  cfg::Config c;
  std::string cerr;
  cfg::load(c, "", &cerr);
  if (!cerr.empty()) fprintf(stderr, "[fanuc_adapter] %s\n", cerr.c_str());

  FanucAdapter *adapter = new FanucAdapter(shdrPort, c.fanuc_scan_delay_ms);
  adapter->setConnectTimeout(c.fanuc_connect_timeout_sec);
  adapter->setReconnectWait(c.fanuc_reconnect_wait_ms);
  adapter->setCommentRetry(c.fanuc_comment_retry_ms);
  adapter->setName("MTConnect Fanuc Adapter");
  return adapter->main(aArgc, aArgv);
}
