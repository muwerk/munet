// telnet.h - munet

#pragma once

#include "ustd_platform.h"
#include "ustd_array.h"
#include "scheduler.h"
#include <Arduino_JSON.h>

#include "console.h"

namespace ustd {

/*! \brief munet Telnet Console Connection Class

The telnet console connection class implements a simple but effective telnet console shell that
allows to communicate to the device via a TCP network connection. See \ref ustd::Console for a
list of supported commands.

This class is not instantiated directly but managed by the \ref TelnetConsole class.

*/

class TelnetConsoleConnection : public Console {
  public:
    WiFiClient client;
    bool connected;
    char buffer[64];

    TelnetConsoleConnection(WiFiClient client)
        : Console("telnet", null), client(client) {
        connected = client.connected();
        printer = &this->client;
    }

    virtual ~TelnetConsoleConnection() {
    }

    virtual String getFrom() {
        return getRemoteAddress() + ":" + getRemotePort();
    }

    virtual String getRemoteAddress() {
        return client.remoteIP().toString();
    }

    virtual String getRemotePort() {
        return String(client.remotePort());
    }

    void begin(Scheduler *_pSched) {
        pSched = _pSched;
        tID = pSched->add([this]() { this->loop(); }, "telnet", 60000);  // 60ms
        init();
        motd();
        prompt();
    }

    virtual void end() {
        client.stop();
        pSched->remove(tID);
    }

    void loop() {
        connected = client.connected();
        if (connected) {
            int icount = client.available();
            while (icount > 0) {
                int iread = min(icount, (int)(sizeof(buffer) / sizeof(char)) - 1);
                int idone = client.read((uint8_t *)buffer, iread);
                if (idone) {
                    const char *pStart = buffer;
                    // our buffer is ALWAYS zero terminated!
                    buffer[idone] = 0;
                    for (char *pPtr = buffer; *pPtr; pPtr++) {
                        switch (*pPtr) {
                        case 4:
                            if (authState == AUTH) {
                                execute("logout");
                            } else {
                                authState = NAUTH;
                            }
                            break;
                        case 9:  // tab
                            // treat like space
                            *pPtr = 32;
                            break;
                        case 10:  // line feed
                        case 13:  // enter
                            // execute the command
                            *pPtr = 0;
                            DBGF("Executing %s\r\n", pStart);
                            args += pStart;
                            pStart = pPtr + 1;
                            DBGF("Rest is now at pos:%i v:%i s:%s\r\n", (int)(pStart - buffer), (int)*pStart, pStart);
                            processInput();
                            prompt();
                            break;
                        }
                    }
                    if (*pStart) {
                        DBGF("Appending rest at pos:%i v:%i s:%s\r\n", (int)(pStart - buffer), (int)*pStart, pStart);
                        args += pStart;
                    }
                    icount -= idone;
                }
            }
        } else {
            if (!finished) {
                DBGF("\rTelnet client disconnected\r\n");
                client.stop();
                finished = true;
            }
        }
    }

    virtual void cmd_logout() {
        Console::cmd_logout();
        client.stop();
        finished = true;
    }
};

/*! \brief munet Telnet Console Class

The telnet console class implements a network server listening on the configured port
and managing \ref TelnetConsoleConnection instances for each incoming connection.

## Sample of telnet remote console:

~~~{.cpp}

#include "scheduler.h"
#include "console.h"
#include "telnet.h"

ustd::Scheduler sched( 10, 16, 32 );
ustd::Net net( LED_BUILTIN );
ustd::SerialConsole con;
ustd::TelnetConsole telnet;

void hurz( String cmd, String args, Print *printer ) {
    printer->println( "Der Wolf... Das Lamm.... Auf der grünen Wiese....  HURZ!" );
    while ( args.length() ) {
        String arg = ustd::shift( args );
        printer->println( arg + "   HURZ!" );
    }
}

void apploop() {}

void setup() {
    // initialize the serial interface
    Serial.begin( 115200 );

    // extend consoles
    con.extend( "hurz", hurz );
    telnet.extend( "hurz", hurz );

    // initialize network services
    net.begin( &sched );

    // initialize consoles
    con.begin( &sched );
    telnet.begin( &sched, &con );

    int tID = sched.add( apploop, "main", 50000 );
}

void loop() {
    sched.loop();
}
~~~

*/
class TelnetConsole : public ExtendableConsole {
  public:
    Scheduler *pSched;
    int tID;
    WiFiServer server;
    int port;
    bool connected = false;
    static uint8_t maxClients;
    static uint8_t numClients;

    TelnetConsole(uint16_t port = 23, uint8_t maxClients = 4)
        : server(port) {
        /*! Instantiate a Telnet console listener
         *
         * @param port (optional, default 23) The port on which the server listens for connections.
         * @param maxClients (optional, default 4) Maximum client connections allowed
         */
        TelnetConsole::maxClients = maxClients;
    }

    virtual ~TelnetConsole() {
    }

    void begin(Scheduler *_pSched) {
        pSched = _pSched;
        tID = pSched->add([this]() { this->loop(); }, "telnet", 60000);  // 60ms
        pSched->subscribe(tID, "net/network", [this](String topic, String msg, String originator) {
            JSONVar jm = JSON.parse(msg);
            if (JSON.typeof(jm) == "object" && JSON.typeof(jm["state"]) == "string") {
                bool newconnected = !strcmp(jm["state"], "connected");
                if (connected != newconnected) {
                    connected = newconnected;
                    if (connected) {
                        DBG("Start listening...");
                        this->server.begin();
                    } else {
                        DBG("Stop listening...");
                        //                        this->server.end();
                    }
                }
            }
        });
    }

    void loop() {
        // accept connections
        if (connected) {
            for (WiFiClient client = server.accept(); client; client = server.accept()) {
                if (numClients < maxClients) {
                    DBGF("\rNew telnet connection from %s:%u\r\n", client.remoteIP().toString().c_str(), client.remotePort());
                    TelnetConsoleConnection *pCon = new TelnetConsoleConnection(client);
                    for (unsigned int i = 0; i < commands.length(); i++) {
                        pCon->extend(commands[i].command, commands[i].fn);
                    }
                    pCon->begin(pSched);
                } else {
                    client.printf("Sorry - maximum connections limit reached. Bye!\n");
                    client.flush();
                    client.stop();
                }
            }
        }
    }
};

// static members initialisation
uint8_t TelnetConsole::maxClients = 4;
uint8_t TelnetConsole::numClients = 0;

}  // namespace ustd
