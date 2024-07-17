#include <MIDI.h>
#include <WiFi.h>
#include <WebServer.h>

// by default this uses UART to transmit MIDI, however any of the transports 
// provided by arduino midi library should work
#define RX_PIN 32
#define TX_PIN 33

// credentials to access point hosted by esp32
const char* ssid = "ESP32-MIDI-Tool";
const char* password = "12345678";     // Enter Password here

// access local network addresses
IPAddress local_ip(192, 168, 32, 1);
IPAddress gateway(192, 168, 32, 1);
IPAddress subnet(255, 255, 255, 0);

// the 3 variables determine "bussiness" logic of the utility and can be changed by
// the user at runtime. if this was a VST, these would be the exposed as parameters
// to the plugin host
//
// switches on/off either of the functionalities
bool enable_random_notes = true;
bool enable_channel_map = true;
// indice & value pairs represent channel mappings
// value at zero-th element is not used, but pads the array for more convienient access
byte channel_map[17] = { 255, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };


// parameters for random note generation
#define RANDOM_NOTE_MAX 73               // inclusive. 73 is highest note TD-3 accepts
#define RANDOM_NOTE_MIN 24               // inclusive. 24 is lowest note TD-3 accepts
// this defines how long the note is held
#define RANDOM_NOTE_ON_DURATION_MS 125   // 16th note @ 120bpm
// this defines the gap between successive notes
#define RANDOM_NOTE_OFF_DURATION_MS 125  // 16th note @ 120bpm
// a generated note is send to each channel in this range at the same time
#define RANDOM_NOTE_CHANNEL_START 1      // inclusive
#define RANDOM_NOTE_CHANNEL_STOP 16      // inclusive

void redirect(WebServer& server) {
  server.sendHeader("Location", "/?r=" + String(random(1111111111)));
  server.send(307, NULL, "");
}

class channel_map_handler_t : public RequestHandler {
  String prefix = "/channel-map/";
  bool canHandle(WebServer& server, HTTPMethod method, String uri) override {
    return method == HTTPMethod::HTTP_GET && uri.startsWith(prefix);
  }
  bool handle(WebServer& server, HTTPMethod requestMethod, String uri) override {
    if (requestMethod == HTTPMethod::HTTP_GET && uri.startsWith(prefix)) {
      String s = uri.substring(prefix.length());
      if (s == "on") {
        enable_channel_map = true;
        redirect(server);
        return true;
      } else if (s == "off") {
        enable_channel_map = false;
        redirect(server);
        return true;
      }
      int i = s.indexOf("/");
      if (-1 == i) {
        server.send(400, "text/plain", "format is: /channel-map/x/y where x and y belong to 1..16 inclusively");
        return true;
      }
      String a = s.substring(0, i);
      String b = s.substring(i + 1);
      int key1 = a.toInt();
      int key2 = b.toInt();
      if (!(key1 >= 1 && key1 <= 16) || !(key2 >= 1 && key2 <= 16)) {
        server.send(400, "text/plain", "format is: /channel-map/x/y where x and y belong to 1..16 inclusively");
        return true;
      }
      channel_map[key1] = key2;
      //server.sendHeader("Location", "/");
      redirect(server);
      return true;
    } else {
      return false;
    }
  }
};
channel_map_handler_t channel_map_handler;

WebServer server(80);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, midi1);

int note_ons = 0;
int note_offs = 0;
int active_random_note = -1;
int random_notes_start_time = -1;

void note_on(int note) {
  for (int channel = RANDOM_NOTE_CHANNEL_START; channel <= RANDOM_NOTE_CHANNEL_STOP; channel++) {
    midi1.sendNoteOn(note, 127, channel);
  }
}

void note_off(int note) {
  for (int channel = RANDOM_NOTE_CHANNEL_START; channel <= RANDOM_NOTE_CHANNEL_STOP; channel++) {
    midi1.sendNoteOff(note, 0, channel);
  }
}

int gen_random_note() {
  return random(RANDOM_NOTE_MIN, RANDOM_NOTE_MAX + 1);
}

void run_random_notes() {
  int now = millis();
  int expected_note_ons = (now - random_notes_start_time) / (RANDOM_NOTE_ON_DURATION_MS + RANDOM_NOTE_OFF_DURATION_MS);
  int expected_note_offs = (now - random_notes_start_time - RANDOM_NOTE_ON_DURATION_MS) / (RANDOM_NOTE_ON_DURATION_MS + RANDOM_NOTE_OFF_DURATION_MS);
  if (enable_random_notes && note_ons < expected_note_ons) {
    assert(active_random_note == -1);
    active_random_note = gen_random_note();
    note_on(active_random_note);
    note_ons = expected_note_ons;
  }
  if (active_random_note != -1 && note_offs < expected_note_offs) {
    note_off(active_random_note);
    active_random_note = -1;
    note_offs = expected_note_offs;
  }
}


void handleNoteOn(byte channel, byte pitch, byte velocity) {
  if (enable_channel_map) {
    midi1.sendNoteOn(pitch, velocity, channel_map[channel]);
  }
}

void handleNoteOff(byte channel, byte pitch, byte velocity) {
  if (enable_channel_map) {
    midi1.sendNoteOff(pitch, velocity, channel_map[channel]);
  }
}

String html_channel_map_enable() {
  const char* webpage_on = R"=====(
  <div>
    <p style="font-family:monospace">Channel mapping: <b>ON</b></p>
    <div>
      <a href="/channel-map/off">
        <p style="font-family:monospace">Click here to turn OFF</p>
      </a>
    </div>
  </div>
)=====";
  const char* webpage_off = R"=====(
  <div>
    <p style="font-family:monospace">Channel mapping: <b>OFF</b></p>
    <div>
      <a href="/channel-map/on">
        <p style="font-family:monospace">Click here to turn <b>ON</b></p>
      </a>
    </div>
  </div>
)=====";
  if (enable_channel_map) {
    return webpage_on;
  } else {
    return webpage_off;
  }
}


String html_random_notes() {
  const char* webpage_on = R"=====(
  <h2>Random notes</h2>
  <div>
    <p style="font-family:monospace">Sending random notes: <b>ON</b></p>
    <div>
      <a href="/random-notes/off">
        <p style="font-family:monospace">Click here to turn <b>OFF</b></p>
      </a>
    </div>
  </div>
)=====";
  const char* webpage_off = R"=====(
  <h2>Random notes</h2>
  <div>
    <p style="font-family:monospace">Sending random notes: <b>OFF</b></p>
    <div>
      <a href="/random-notes/on">
        <p style="font-family:monospace">Click here to turn <b>ON</b></p>
      </a>
    </div>
  </div>
)=====";
  if (enable_random_notes) {
    return webpage_on;
  } else {
    return webpage_off;
  }
}

String div(String cls, String content) {
  return "<div class=\"" + cls + "\">\n" + content + "\n</div>\n";
}

String a(String href, String content) {
  return "<a href=\"" + href + "\">\n" + content + "\n</a>\n";
}

String html_channel_map() {
  String section = "";
  for (int i = 1; i <= 16; i++) {
    String row = "";
    String x = div("col", String(i));
    String y = div("col", "=>");
    String z = div("col", String(channel_map[i]));
    row += x + y + z;
    for (int j = 1; j <= 16; j++) {
      String cls = channel_map[i] == j ? "col hl" : "col";
      row += div(cls, a("/channel-map/" + String(i) + "/" + String(j), String(j)));
    }
    row = div("row", row);
    section += row;
  }
  section = "<section>\n" + section + "\n</section>\n";
  return "<h2>Channel map</h2>\n" + html_channel_map_enable() + section;
}

String html_main() {
  const char* head = R"=====(
<!DOCTYPE html>
<html>

<head>

  <head>
   <style>
        section {
        display: table;
      }

      section>* {
        display: table-row;
      }

      section .col {
        display: table-cell;
        padding-right: 10px;
      }

      :link {
        color: #000000;
      }

      :visited {
        color: #000000;
      }

      :link:active,
      :visited:active {
        color: #FF0000;
      }

      * {
        font-family: monospace;
      }
    </style>
    <title>ESP32 MIDI Util</title>
  </head>

<body>
  <h1>ESP32 MIDI Util</h1>
)=====";
  const char* tail = R"=====(
</body>

</html>
)=====";
  return head + html_random_notes() + html_channel_map() + tail;
}

void http_handle_root() {
  server.send(200, "text/html", html_main());
}

void http_handle_random_notes_off() {
  enable_random_notes = false;
  redirect(server);
}

void http_handle_random_notes_on() {
  enable_random_notes = true;
  redirect(server);
}

void setup() {
  // start normal Serial normally
  Serial.begin(115200);

  // start Serial1 on RX=32 TX=33 pins at MIDI baud rate
  Serial1.begin(31250, 1, RX_PIN, TX_PIN);

  // redirect note events from channel 10 to 3
  // channel_map[10] = 3;

  midi1.begin(MIDI_CHANNEL_OMNI);
  midi1.setHandleNoteOn(handleNoteOn);
  midi1.setHandleNoteOff(handleNoteOff);


  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  delay(100);


  server.on("/", http_handle_root);
  server.on("/random-notes/off", http_handle_random_notes_off);
  server.on("/random-notes/on", http_handle_random_notes_on);
  server.addHandler(&channel_map_handler);
  server.begin();
}

void loop() {
  midi1.read();
  run_random_notes();
  server.handleClient();
  delay(1);
}