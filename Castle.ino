/*
   Castle Internet Dialler - Alarm Dialler - Especially suited to Aritech Alarm Panels 
   
   For Arduino (UNO or Leonardo) with added Ethernet Shield
   
   See Circuit Diagram for wiring instructions

   Supports Event Print 
   Sketch for IDE v1.0.1 and w5100/w5200 or Atmel Studio
   Posted January 2014 by Ambrose Clarke (Ozmo)
*/


//uncomment this to remove all RS232 Logging
//#define QUIET                

#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>

//Workaround if needed for http://gcc.gnu.org/bugzilla/show_bug.cgi?id=34734
//#ifdef PROGMEM
//#undef PROGMEM
//#define PROGMEM __attribute__((section(".progmem.data")))
//#endif


//--------Configuration Start----------
        
//change network settings to yours
IPAddress ip( 192, 168, 1, 205 );                //Give the device a unique IP
IPAddress gateway( 192, 168, 1, 1 );        //Gateway (the Router)

//---To Send emails You NEED get the IP address of the SMTP server from the list below that matches your Internet Provider

//"smtp.mysmart.ie"                                                        Smart Telecom Outgoing SMTP Server
//"smtp.irishbroadband.ie."                                        Irish Broadband Outgoing SMTP Server
//mail1.eircom.net OR mail2.eircom.net                Eircom Outgoing SMTP Server
//smtp.magnet.ie                                                        Magnet Outgoing SMTP Server
//smtp.upcmail.ie                                                        NTL and UPC Outgoing SMTP Server
//mail.icecomms.net                                                        Ice Broadband Outgoing SMTP Server
//mail.vodafone.ie                                                        Vodafone Outgoing SMTP Server
//smtp.o2.ie                                                                O2 Outgoing SMTP Server
//smtp.clearwire.ie / mail.clearwire.ie                Clearwire Outgoing SMTP Server
//smtp.digiweb.ie                                                        Digiweb Outgoing SMTP Server
//mail.imagine.ie OR mail.gaelic.ie                        Imagine Broadband Outgoing SMTP Server
//mail.perlico.ie                                                        Perlico Outgoing SMTP Server
//mail-relay.3ireland.ie                                        3 Outgoing SMTP Server: Mobile broadband with 3 mobile Ireland
char SMTPServer[] = "213.46.255.2";                 //this is the IP for "smtp.upcmail.ie" 

int nSerialBaud = 9600;                //1200; //Some panels have max of 2400 baud 

const String sEmail = "yourname@example.com"; //change this to your email address

// this sequence must be unique on your lan 
byte mac[] = { 0x90, 0xA2, 0xDA, 0x00, 0x59, 0x67 };        //typically dont need change
IPAddress subnet( 255, 255, 255, 0 );        //typically dont need change


#define analogChannelMax 6
//These are analog inputs you can use to read values and display on the webpage
//Rename these to whatever you like - they allow you to monitor voltages 0..5v (water level/Armed/Disarmed outputs etc)
String chanelNames[analogChannelMax]={"1","2","3","4","5","6"};

//We will default to 3 outputs (ideal for connecting to panel input zones or other) - 2 for arm disarm and a third for user to choose the purpose
#define zoneNamesMax 3
String zoneNames[zoneNamesMax]={"Action1","Action2","Action3"};
int zonePins[zoneNamesMax]={5,6,7};

//So Pin4 will be connect to the center of two 4k7 resistors connected to a Panels "Key Switch" Zone
#define armPin 4


//--------Configuration Ends----------

//Build Version (displayed on webpage)
const String sVersion="V1.3";


//Internal Timing Variables
EthernetClient client;
int nEmailStage=-1;
int mDelay=-1;
int mTimeout = 200; //200 = 2secs
boolean bWaitForResponse = false;

//Flashing Led we can use to show activity
int ledFeedback = 13;
int ledFeedbackState = HIGH;
int tiFlip = 0;

//For the server
EthernetServer WWWServer(80);


// the current address in the EEPROM (i.e. which byte we're going to write to next)
const int EELogStart = 16; //reserve first 16 bytes for other uses - rest for Logs
const int EELogLineLen = 80; //Alarm is set to print @80 chars per line
int EELogIX = EELogStart; //pointer to next line to be written

//Memory Map
const int MMAP_EEINIT = 0;
const int MMAP_NextLine = 1;        //eeprom addr where location of next line will be stored

//The line being read from the Alarm Panel
byte sCurrentLine[EELogLineLen];
byte nCurrentChar = 0;

void sendEmailProcess();
void efail();



#ifdef QUIET
void LogLn(String s){}
void LogLn(String s){}
void LogLn(char c){}
void LogLn(IPAddress c){}
void Log(String s){}
void Log(char c){}
void LogHex(char* s){}
void LogHex(byte* s , int len){}
void Log(int n){}
#else
void LogLn(String s){Serial1.println(s);}
void LogLn(String s, int n){Serial1.print(s);Serial1.println(n);}
void LogLn(char c){Serial1.println(c);}
void LogLn(IPAddress c){Serial1.println(c);}
void Log(String s){Serial1.print(s);}
void Log(char c){Serial1.print(c);}
void Log(int n){Serial1.print(n);}
void LogHex(char* s)
{
        int n=0;
        while(s[n] != 0)
        {
                Serial1.print(s[n], HEX);
                Serial1.print(' ');
        }
        Serial1.println("{end}");
}
void LogHex(byte* s , int len)
{
        const int l = 16;
        for(int col=0;;col++)
        {
                for(int r=0;r<l;r++)
                {
                        if (r!=0)
                                Serial1.print(' ');
                        byte c = s[col*l+r];
                        if (c<16)
                                Serial1.print('0');
                        Serial1.print(c, HEX);
                }
                Serial1.print(':');
                for(int r=0;r<l;r++)
                {
                        byte c = s[col*l+r];
                        if (c <= ' ' || c>= 128)
                                Serial1.print('.');
                        else
                                Serial1.print((char)c);
                }
                Serial1.println("");
                len-=8;
                if (len<0)
                        break;
        }

        Serial1.println("{end}");
}
#endif

//Call this to send the Log file out as email
void queueEmail()
{
        LogLn("Sending Email.");
        nEmailStage=0;
}

//Blanks the line with spaces and sets nCurrentChar to start again.
void ResetLineBuffer()
{
        nCurrentChar = 0;
        for(int n=0;n<EELogLineLen;n++)
                sCurrentLine[n]=' ';
}

//Blanks the inmemory Log file (actually just resets internal pointers)
void EEPromInit()
{
        EEPROM.write(MMAP_NextLine, 0);        //Log start
        EEPROM.write(MMAP_EEINIT, 0xAC); //marker to say was initialized
}

void setup()
{
        //Serial In and Debug Out Setup        (alarm outputs at 9600)
        Serial1.begin(nSerialBaud,SERIAL_8N1); //2400
                
        //Wait for serial port to connect. Supposed to be needed for Leonardo- but it can stall so best not use
        //while (!Serial1) {;}

        LogLn("-----[Start]-----");
        
        //EEProm Setup if a new Arduino
        if (EEPROM.read(MMAP_EEINIT)!= 0xAC)
                EEPromInit();

        //Line Buffer Setup
        ResetLineBuffer();

        //Flashing led
        pinMode(ledFeedback,OUTPUT); digitalWrite(ledFeedback,ledFeedbackState);

        //Start Ethernet
        Ethernet.begin(mac, ip, dns, gateway, subnet);
        //Ethernet.begin(mac, ip, gateway, gateway, subnet); 
        
        //Start the admin server
        WWWServer.begin();
        Log("server is at "); LogLn(Ethernet.localIP());
        
        //Will allow remote triggering pins 5 to 7 - can trigger as many as you like
        //but 0,1,2 may be reserved by memory card depending on ethercard shield revision
        //8 or 10 may be reserved by SPI bus
        //The First two outputs we will use for Arm Disarm
        pinMode(armPin, INPUT); //Set to high-impedance state (disconnected)
        for(int n=0;n<zoneNamesMax;n++)
        {
                int p = zonePins[n];
                pinMode(p, OUTPUT);
        }
        
        //queueEmail(); //Test email
        LogLn("Ready "+sVersion);
 }

//Checks a completed line from the panel if its an alarm
void CheckRXForAlarms()
{
        const int strlen_sMatch = 5;
        for(int n=0;n < EELogLineLen-strlen_sMatch; n++)
        {
                if (sCurrentLine[n] == 'A'
                && sCurrentLine[n+1] == 'l'
                && sCurrentLine[n+2] == 'a'
                && sCurrentLine[n+3] == 'r'
                && sCurrentLine[n+4] == 'm')
                        queueEmail();
        }
}

//True if ASCII is 0..9
bool notDigit(char c){return c <'0' && c > '9';}
//True if Char is Not an Uppercase letter
bool notUCase(char c){return c <'A' && c > 'Z';}
//True if Char is Not an Lowercase letter
bool notLCase(char c){return c <'a' && c > 'z';}

//Getting a lot of noise on the RS232 when no panel connected
//So check its a valid log before we write to memory
bool CheckValidLogFromPanel()
{
        //Valid line looks like this - we can verify time stamp looks ok
        //24 Apr 12:01 EH Eng Here 1st Engineer
        boolean bValid = true;
        String sFmt="99 Aaa 99:99 ";
        int nFmtLen = sFmt.length();
        if (nCurrentChar < nFmtLen)
        {
                LogLn("Invalid Log Entry. Length incorrect");
                return false;
        }
        int n=-1;
        for(n=0;n<nFmtLen && bValid != false;n++)
        {
                char c = sFmt[n];
                if (c=='9' && notDigit(sCurrentLine[n]))
                        bValid = false;
                else if (c==' ' && sCurrentLine[n] != c)
                        bValid = false;
                else if (c==':' && sCurrentLine[n] != c)
                        bValid = false;
                else if (c=='A' && notUCase(sCurrentLine[n]))
                        bValid = false;
                else if (c=='a' && notLCase(sCurrentLine[n]))
                        bValid = false;
        }
        if (!bValid)
                LogLn("Log entry format not recognised. Char ", n);
        return bValid;
}

//read a character - write it to flash if have a complete line - reset buffer
void readRxCharacter()
{
        byte rx = Serial1.read();
        
        boolean bDoE2Write = false;
        if (rx=='\r' || rx=='\n')
        {//End line - write to e2 (if we have something to write)
                LogLn("{R}");
                if (nCurrentChar>0)
                        bDoE2Write = true;
        }
        else
        {
                Log((char)rx);
                if (nCurrentChar < EELogLineLen) //Shouldnt be necessary
                        sCurrentLine[nCurrentChar++]= rx;
                else
                        Log("Int Error - Overspill");
                        
                //only store 80chars then write line to log
                if (nCurrentChar >= EELogLineLen)
                        bDoE2Write= true;
        }
        
        if (bDoE2Write)
        {//Write line to flash
                if (CheckValidLogFromPanel())
                {
                        LogLn("\r\n{Writing Line}");
                        int EELogIX=EEPROM.read(MMAP_NextLine)*EELogLineLen+EELogStart;
                        for(int n=0;n<EELogLineLen;n++)
                                EEPROM.write(EELogIX++,sCurrentLine[n]);
                        
                        //overspill?
                        if ((EELogIX + EELogLineLen) >= E2END)
                                EELogIX = EELogStart;

                        //Write New address (line number)
                        int nxtLine = (EELogIX-EELogStart) / EELogLineLen;
                        EEPROM.write(MMAP_NextLine,nxtLine);
                
                        //Check for alarms
                        CheckRXForAlarms();
                }
                //blank buffer for next line
                ResetLineBuffer();
        }
}

//Allows us to see home much ram we have left to play with
static int freeRam () {
        extern int __heap_start, *__brkval;
        int v;
        return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}


boolean waitForReplyLine()
{
        if(client.connected() && client.available())
        {
                byte thisByte = client.read();
                //Log(thisByte);
                if (thisByte == '\n')
                {
                        nEmailStage++;
                        bWaitForResponse = false;
                        return true;        //Good!
                }
        }
        return false;
}

//TODO: this will implement DES encrypted login - marginally better than no security at all.
void SendWWWReplyUnauth(EthernetClient client)
{
        client.println("HTTP/1.1 401 Unauthorized");
        //client.println("WWW-Authenticate: Digest realm=\"Castle\" location=\"http://login\"");
        client.println("WWW-Authenticate: Digest realm=\"Castle\",nonce=\"NXa26+NjBAA=747dfd1776c9d585bd388377ef3160f1ff265429\"");
        client.println("Connection: close");  // the connection will be closed after completion of the response
        client.println();

        //TODO: Implement DES Authorisation
        //Server sends
        //
        //                HTTP/1.1 401 Unauthorized
        //                        WWW-Authenticate: Digest
        //                        realm="testrealm@host.com",
        //                        qop="auth,auth-int",
        //                        nonce="dcd98b7102dd2f0e8b11d0f600bfb0c093",
        //                        opaque="5ccc069c403ebaf9f0171e9517f40e41"
        //Response is
        //                Authorization: Digest username="Mufasa",
        //                        realm="testrealm@host.com",
        //                        nonce="dcd98b7102dd2f0e8b11d0f600bfb0c093",
        //                        uri="/dir/index.html",
        //                        qop=auth,
        //                        nc=00000001,
        //                        cnonce="0a4f113b",
        //                        response="6629fae49393a05397450978507c4ef1",
        //                        opaque="5ccc069c403ebaf9f0171e9517f40e41"
                                                                
        //Response is
        //                Authorization: Digest username="aaa", realm="Castle", nonce="NXa26+NjBAA=747dfd1776c9d585bd388377ef3160f1ff265429", uri="/?Z7=", response="de900ee389c8fe5637bd0572bc11bc8d"
}


void ToggleZones(int action)
{
        //Toggle Zone
        if (action=='a')
        {//Arm
                //Grounded Pin 
                digitalWrite(armPin, LOW);
                pinMode(armPin, OUTPUT); //Set to Ground this Pin - adjusting resistance to 1 4k7 resister
                LogLn("Arm");
        }
        if (action=='d')
        {//Disarm
                //Set so Pin is Not Grounded
                pinMode(armPin, INPUT); //Set to high-impedance state (disconnected) so resistance is 2 4k7 resisters
                LogLn("DisArm");
        }
        
        //and set the rest of the zone outputs
        for(int n=0;n<zoneNamesMax;n++)
        {
                int pin = zonePins[n];
                digitalWrite(pin,  (pin == action)? HIGH : LOW);
        }
}

//This is the webpage...
void SendWWWReply(EthernetClient client, int zone, char action)
{
        LogLn("Received WWW Request");
                                
        // send a standard http response header
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
                                
        client.println("Connection: close");  // the connection will be closed after completion of the response
        //client.println("Refresh: 5");  // refresh the page automatically every 5 sec
        client.println();
        client.println("<!DOCTYPE HTML>");
        client.println("<html>");
        client.println("<head/><body style=\"font-family: 'Courier New', Courier, monospace\">");
        
        {
                //String zoneNames[zoneNamesMax]={"Action1","Action2","Action3"};
                //int zonePins[zoneNamesMax]={5,6,7};
                //#define armPin 4
                client.println("<form method=get>");
                for(int n=0;n<zoneNamesMax;n++)
                {
                        client.print("<button name='z' ");
                        client.print("value='"); client.print(zonePins[n]); client.print("'>");
                        client.print(zoneNames[n]);client.println("</button>&nbsp;");
                }
                client.println("<hr>");
                client.print("<button name='a' value='a'>Arm!</button>&nbsp;");
                client.print("<button name='a' value='d'>DisArm!</button>&nbsp;");
                
                client.println("</form><hr>");

                //client.println("<form method=get><button name='Z5'>5</button><button name='Z6'>6</button><button name='Z7'>7</button></form>");
        }
                
        // output the value of each analog input pin
        for (int analogChannel = 0; analogChannel < analogChannelMax; analogChannel++)
        {
                int sensorReading = analogRead(analogChannel);
                client.print("analog input ");
                client.print(chanelNames[analogChannel]);
                client.print(" is ");
                client.print(sensorReading);
                client.println("<br/>");
        }
                                
        if (zone>=0 || action>=0)
                client.println("<hr>");
        if (zone!=-1)
        {//Request to pulse a zone
                client.print("Zone: "); client.println(zone); client.println("<br>");
        }
        if (action!=-1)
        {
                client.println("Action: "); client.println((char)action);
        }
                                
        client.println("<hr/><h2>Log</h2>");
        int startLine = EEPROM.read(MMAP_NextLine); //next to overwrite is the oldest and the fist we should start the log report with
        int logLineCount = (int)((E2END-EELogStart)/EELogLineLen);
        int ix = EELogStart + startLine * EELogLineLen;
                                
        for(int n=0;n<logLineCount;n++)
        {
                client.print(n); client.print(")");
                char b = EEPROM.read(ix);
                if ((byte)b == 0xFF)
                        client.print("Empty");
                else
                        for(int c = 0; c < EELogLineLen; c++)
                        {
                                if(c!=0)        //if 0 then we already have it read - do not wear out the EEProm unnecessarily
                                        b = EEPROM.read(ix+c);
                                client.print(b);
                        }
                client.println("<br/>");
                ix+= EELogLineLen;
                if ((ix+EELogLineLen)>= E2END)
                        ix = EELogStart;
        }
        
        //----Debug info-------
        {
                //#ifdef DEBUG
                long t = millis() / 1000;
                int h = t / 3600;
                int m = (t / 60) % 60;
                int s = t % 60;

                client.print("Uptime is ");
                client.print(h); client.print("hrs "); client.print(m); client.print("mins "); client.print(s); client.println("secs. ");
                client.print("MemoryFree: "); client.println(freeRam()); client.print(" PrinterBaud: "); client.println(nSerialBaud);
                
                //client.print("Start:"); client.println(startLine);client.print("<br/>");
                //client.print("LogSpace:"); client.println(E2END-EELogStart);client.print("<br/>");
                //client.print("logLineCount:"); client.println(logLineCount);client.print("<br/>");
                //#endif
        }
        //----Debug info-------
                        
        client.println("</body></html>");

}


//Someone has browsed to our server - show them the log page
void attachNewClient(EthernetClient client)
{
        LogLn("new client");
        // an http request ends with a blank line
        boolean currentLineIsBlank = true;

        int zone = -1; char action = -1;
        char cPrev1 = 0;char cPrev2 = 0;
        boolean bFirstLine=true;
        while (client.connected())
        {
                if (client.available())
                {
                        char c = client.read();
                        if (bFirstLine)
                        {
                                if (cPrev2=='z' && cPrev1=='=')
                                        if (c>'0' && c <'9')
                                                zone = c-'0';
                                if (cPrev2=='a' && cPrev1=='=')
                                        action = c;
                                cPrev2 = cPrev1; cPrev1 = c;
                                if (c == '\n')
                                        bFirstLine=false;
                        }
                        
                        // if you've gotten to the end of the line (received a newline character) and the line is blank, the http request has ended,
                        if (c == '\n' && currentLineIsBlank)
                        {
                                SendWWWReply(client, zone, action);

                                ToggleZones(zone);
                                
                                break;
                        }
                        if (c == '\n')
                                currentLineIsBlank = true;        // you're starting a new line
                        else if (c != '\r')
                                currentLineIsBlank = false;// you've gotten a character on the current line
                }
        }
        
        // give the web browser time to receive the data
        delay(1);
        // close the connection:
        client.stop();
        LogLn("client disconnected");
        Log("Action ="); LogLn(action);
}

/*
  SerialEvent occurs whenever a new data comes in the
 hardware serial RX.  This routine is run between each
 time loop() runs, so using delay inside loop can delay
 response.  Multiple bytes of data may be available.
 */
//String inputString = "";         // a string to hold incoming data
void serialEvent()
{
/*        Log("!");
        while (Serial1.available())
        {
                // get the new byte:
                char inChar = (char)Serial1.read(); 
                // add it to the inputString:
                inputString += inChar;
                // if the incoming character is a newline, set a flag so the main loop can do something about it:
                if (inChar == '\n')
                        stringComplete = true;
        }
        */
}


void loop()
{
        //Note - nothing here is to be blocking 
        //- so sending emails must not block receiving serial logs
        if (Serial1.available() > 0)
                readRxCharacter();

        //Send Email if Alarm
        if(nEmailStage >= 0)        
                sendEmailProcess();
        
        //Report via HTTP        
        EthernetClient client = WWWServer.available();
        if (client)
                attachNewClient(client);


        //Flash status led                
        int tiNow = millis();
        if ((tiNow - tiFlip) > 1000 )
        {
                tiFlip = tiNow;
                ledFeedbackState = ledFeedbackState == HIGH? LOW:HIGH;
                digitalWrite(ledFeedback,ledFeedbackState);
        }
}

//Processing any outstanding emails to be sent
void sendEmailProcess()
{
        delay(1); //loop once each millisecond
        
        if(mTimeout > 0)
                mTimeout--;
        if(mTimeout == 0)
        {//waiting for 10 secs - then reset and resend
                LogLn("Reseting...");
                mTimeout=5000; //dont reset again
                nEmailStage=0;
                client.println("QUIT");
                mDelay=-1;
                //return;
        }

        if (mDelay>=0)
        {//Delay on..?
                mDelay--;
                delay(100);
                return;
        }
        
        if (bWaitForResponse)
        {//To Wait for response
                waitForReplyLine();
                return;
        }
        
        if (nEmailStage==0)
        {//Connect
                LogLn("--Start SendMail--");
                
                Log("Cleaning buffers..{");
                while(client.available())
                {
                        byte thisByte = client.read();
                        Log(thisByte);
                }
                LogLn("}Cleaned");
                
                LogLn("Connecting...");
                if(!client.connect(SMTPServer,25) /*|| !client.connected()*/)
                {
                        LogLn("connection failed");
                        mDelay=10; //keep trying once per sec
                        return;
                }
                LogLn("Confirmed Connected");
                bWaitForResponse=true;
                mTimeout = 10000; //10 sec to send...or will resend
                return;
        }
        
        if (nEmailStage==1)
        {
                LogLn("Sending helo");
                client.println("helo 1.2.3.4");
                bWaitForResponse=true;
                return;
        }

        if (nEmailStage==2) //set to 20 to test retry
        {
                LogLn("Sending From");
                client.println("MAIL From: "+sEmail);        //(sender)
                bWaitForResponse=true;
                return;
        }

        if (nEmailStage==3)
        {
                //recipient address
                LogLn("Sending To");
                client.println("RCPT To: "+sEmail);                //client.println("RCPT To: <ambrosec@gmail.com>");
                bWaitForResponse=true;
                return;
        }

        if (nEmailStage==4)
        {
                LogLn("Sending DATA");
                client.println("DATA");
                bWaitForResponse=true;
                return;
        }
        
        if (nEmailStage==5)
        {
                LogLn("Sending email");
                client.println("To: "+ sEmail);
                client.println("From: TheHouse <"+sEmail+">");
                client.println("Subject: House Calling. Alarm.\r\n");
                
                //client.println("This is from my Arduino!");
                //client.println("Yay!");
                
                {
                        client.println("The House Alarm has gone off\r\nLog\r\n===");
                        int startLine = EEPROM.read(MMAP_NextLine); //next to overwrite is the oldest and the fist we should start the log report with
                        int logLineCount = (int)((E2END-EELogStart)/EELogLineLen);
                        int ix = EELogStart + startLine * EELogLineLen;
                                                        
                        for(int n=0;n<logLineCount;n++)
                        {
                                client.print(n); client.print(")");
                                char b = EEPROM.read(ix);
                                if ((byte)b == 0xFF)
                                        client.print("Empty");
                                else
                                for(int c = 0; c < EELogLineLen; c++)
                                {
                                        if(c!=0)        //if 0 then we already have it read - do not wear out the EEProm unnecessarily
                                        b = EEPROM.read(ix+c);
                                        client.print(b);
                                }
                                client.println("\r\n");
                                ix+= EELogLineLen;
                                if ((ix+EELogLineLen)>= E2END)
                                ix = EELogStart;
                        }
                        client.println("EndLog");
                }
                
                client.println(".");
                bWaitForResponse=true;
                return;
        }

        if (nEmailStage==6)
        {
                mTimeout = 20000; //cancel any timeout timer
                LogLn("Sending QUIT");
                client.println("QUIT");
                bWaitForResponse=true;
                return;
        }
        
        if (nEmailStage==7)
        {
                client.stop();
                nEmailStage=-1;
                LogLn("disconnected");
                return;
        }
}
