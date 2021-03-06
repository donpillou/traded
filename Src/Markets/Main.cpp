
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#endif

#include <nstd/Console.h>
#include <nstd/Thread.h>
#include <nstd/Directory.h>
#include <nstd/Error.h>

#include "Tools/RelayConnection.h"

#ifdef MARKET_BITSTAMPBTCUSD
#include "Markets/BitstampBtcUsd.h"
typedef BitstampBtcUsd MarketConnection;
const char* exchangeName = "BitstampBtcUsd";
#endif
#ifdef MARKET_MTGOXBTCUSD
#include "Markets/MtGoxBtcUsd.h"
typedef MtGoxBtcUsd MarketConnection;
const char* exchangeName = "MtGoxBtcUsd";
#endif
#ifdef MARKET_HUOBIBTCCNY
#include "Markets/HuobiBtcCny.h"
typedef HuobiBtcCny MarketConnection;
const char* exchangeName = "HuobiBtcCny";
#endif
#ifdef MARKET_BTCCHINABTCCNY
#include "Markets/BtcChinaBtcCny.h"
typedef BtcChinaBtcCny MarketConnection;
const char* exchangeName = "BtcChinaBtcCny";
#endif
#ifdef MARKET_BITFINEXBTCUSD
#include "Markets/BitfinexBtcUsd.h"
typedef BitfinexBtcUsd MarketConnection;
const char* exchangeName = "BitfinexBtcUsd";
#endif
#ifdef MARKET_BTCEBTCUSD
#include "Markets/BtceBtcUsd.h"
typedef BtceBtcUsd MarketConnection;
const char* exchangeName = "BtceBtcUsd";
#endif
#ifdef MARKET_KRAKENBTCUSD
#include "Markets/KrakenBtcUsd.h"
typedef KrakenBtcUsd MarketConnection;
const char* exchangeName = "KrakenBtcUsd";
#endif

int_t main(int_t argc, char_t* argv[])
{
  static const uint16_t port = 40123;
  bool background = true;
  String dataDir("Data");

  // parse parameters
  for(int i = 1; i < argc; ++i)
    if(String::compare(argv[i], "-f") == 0)
      background = false;
    else if(String::compare(argv[i], "-c") == 0&& i + 1 < argc)
    {
      ++i;
      dataDir = String(argv[i], String::length(argv[i]));
    }
    else
    {
      Console::errorf("Usage: %s [-f] [-c <dir>]\n\
  -f            run in foreground (not as daemon)\n\
  -c <dir>      set data directory (default is .)\n", argv[0]);
      return -1;
    }

#ifndef _WIN32
    // change working directory
  if(!Directory::exists(dataDir) && !Directory::create(dataDir))
  {
    Console::errorf("error: Could not create data directory: %s\n", (const tchar_t*)Error::getErrorString());
    return -1;
  }
  if(!dataDir.isEmpty() && !Directory::change(dataDir))
  {
    Console::errorf("error: Could not enter data directory: %s\n", (const tchar_t*)Error::getErrorString());
    return -1;
  }
#endif

#ifndef _WIN32
  // daemonize process
  if(background)
  {
    char logFile[200];
    strcpy(logFile, exchangeName);
    strcat(logFile, ".log");

    Console::printf("Starting as daemon...\n");

    int fd = open(logFile, O_CREAT | O_WRONLY |  O_CLOEXEC, S_IRUSR | S_IWUSR);
    if(fd == -1)
    {
      Console::errorf("error: Could not open file %s: %s\n", logFile, strerror(errno));
      return -1;
    }
    if(dup2(fd, STDOUT_FILENO) == -1)
    {
      Console::errorf("error: Could not reopen stdout: %s\n", strerror(errno));
      return 0;
    }
    if(dup2(fd, STDERR_FILENO) == -1)
    {
      Console::errorf("error: Could not reopen stderr: %s\n", strerror(errno));
      return 0;
    }
    close(fd);

    pid_t childPid = fork();
    if(childPid == -1)
      return -1;
    if(childPid != 0)
      return 0;
  }
#endif

  RelayConnection relayConnection;
  MarketConnection marketConnection;
  String channelName = marketConnection.getChannelName();

  class Callback : public Market::Callback
  {
  public:
    virtual bool_t receivedTrade(const Market::Trade& trade)
    {
      if(!relayConnection->sendTrade(trade))
        return false;
      return true;
    }

    virtual bool_t receivedTime(uint64_t time)
    {
      if(!relayConnection->sendServerTime(time))
        return false;
      return true;
    }

    virtual bool_t receivedTicker(const Market::Ticker& ticker)
    {
      if(!relayConnection->sendTicker(ticker))
        return false;
      return true;
    }

    RelayConnection* relayConnection;
  } callback;
  callback.relayConnection = &relayConnection;

  for(;; Thread::sleep(10 * 1000))
  {
    if(!relayConnection.isOpen())
    {
      Console::printf("Connecting to relay server...\n");
      if(!relayConnection.connect(port, channelName))
      {
        Console::printf("Could not connect to relay server: %s\n", (const char_t*)relayConnection.getErrorString());
        continue;
      }
      else
        Console::printf("Connected to relay server.\n");
    }

    if(!marketConnection.isOpen())
    {
      Console::printf("Connecting to %s...\n", (const char_t*)channelName);
      if(!marketConnection.connect())
      {
        Console::printf("Could not connect to %s: %s\n", (const char_t*)channelName, (const char_t*)marketConnection.getErrorString());
        continue;
      }
      else
        Console::printf("Connected to %s.\n", (const char_t*)channelName);
    }

    for(;;)
      if(!marketConnection.process(callback))
        break;

    if(!relayConnection.isOpen())
      Console::printf("Lost connection to relay server: %s\n", (const char_t*)relayConnection.getErrorString());
    if(!marketConnection.isOpen())
      Console::printf("Lost connection to %s: %s\n", (const char_t*)channelName, (const char_t*)marketConnection.getErrorString());
    marketConnection.close(); // reconnect to reload the trade history
  }

  return 0;
}
