#include <ProjectSampahAI_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>

// =====================================================
// WIFI
// =====================================================

const char* WIFI_SSID = "YNWA 8213";
const char* WIFI_PASSWORD = "6|H0i674";

WebServer server(80);

// =====================================================
// SERVO
// =====================================================

Servo servoPintuAtas;
Servo servoPusingan;

const int pinServoPintu = 14;
const int pinServoPusing = 20;

// =====================================================
// STATUS SISTEM UNTUK WEB SERVER
// =====================================================

String webDetectedTrash = "Tiada";
float webConfidence = 0.0;

String webStatus = "Menunggu objek...";
String webServoStatus = "Sedia";
int webServoAngle = 45;

unsigned long lastDetectionTime = 0;

// =====================================================
// KONFIGURASI KAMERA ESP32-S3
// =====================================================

#define CAMERA_MODEL_CUSTOM

#if defined(CAMERA_MODEL_CUSTOM)

  #define PWDN_GPIO_NUM    -1
  #define RESET_GPIO_NUM   -1

  #define XCLK_GPIO_NUM    15

  #define SIOD_GPIO_NUM    4
  #define SIOC_GPIO_NUM    5

  #define Y9_GPIO_NUM      16
  #define Y8_GPIO_NUM      17
  #define Y7_GPIO_NUM      18
  #define Y6_GPIO_NUM      12
  #define Y5_GPIO_NUM      10
  #define Y4_GPIO_NUM      8
  #define Y3_GPIO_NUM      9
  #define Y2_GPIO_NUM      11

  #define VSYNC_GPIO_NUM   6
  #define HREF_GPIO_NUM    7
  #define PCLK_GPIO_NUM    13

#else

  #error "Camera model not selected"

#endif

// =====================================================
// EDGE IMPULSE CAMERA
// =====================================================

#define EI_CAMERA_RAW_FRAME_BUFFER_COLS 320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS 240
#define EI_CAMERA_FRAME_BYTE_SIZE 3

static bool debug_nn = false;
static bool is_initialised = false;

uint8_t *snapshot_buf;

// =====================================================
// CAMERA CONFIG
// =====================================================

static camera_config_t camera_config = {

    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,

    .pin_xclk = XCLK_GPIO_NUM,

    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,

    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,

    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,

    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,

    .frame_size = FRAMESIZE_QVGA,

    .jpeg_quality = 12,

    .fb_count = 1,

    .fb_location = CAMERA_FB_IN_PSRAM,

    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

// =====================================================
// FUNCTION DECLARATIONS
// =====================================================

bool ei_camera_init(void);
void ei_camera_deinit(void);

bool ei_camera_capture(
    uint32_t img_width,
    uint32_t img_height,
    uint8_t *out_buf
);

static int ei_camera_get_data(
    size_t offset,
    size_t length,
    float *out_ptr
);

void bukaDanTutupPintu();

void setupWiFi();
void setupWebServer();

void handleRoot();
void handleStatus();


// =====================================================
// WIFI SETUP
// =====================================================

void setupWiFi()
{
    Serial.println();
    Serial.println("==========================================");
    Serial.println("             SAMBUNG WIFI");
    Serial.println("==========================================");

    WiFi.mode(WIFI_STA);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Menyambung ke WiFi");

    int attempts = 0;

    while (WiFi.status() != WL_CONNECTED && attempts < 30)
    {
        delay(500);

        Serial.print(".");

        attempts++;
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("[WIFI] Berjaya disambungkan!");

        Serial.print("[WIFI] Nama WiFi: ");
        Serial.println(WIFI_SSID);

        Serial.print("[WIFI] IP Address: ");
        Serial.println(WiFi.localIP());

        Serial.println();
        Serial.println("Buka alamat ini pada telefon/laptop:");
        Serial.print("http://");
        Serial.println(WiFi.localIP());

        webStatus = "WiFi tersambung";
    }
    else
    {
        Serial.println("[WIFI] Gagal sambung WiFi!");

        webStatus = "WiFi gagal";
    }
}


// =====================================================
// WEB SERVER
// =====================================================

void setupWebServer()
{
    // Halaman utama
    server.on("/", HTTP_GET, handleRoot);

    // JSON status
    server.on("/status", HTTP_GET, handleStatus);

    server.begin();

    Serial.println("[WEB SERVER] Web server telah bermula.");
}


// =====================================================
// WEB PAGE
// =====================================================

void handleRoot()
{
    String html =
        "<html>"
        "<head>"
        "<meta name='viewport' content='width=device-width'>"
        "<meta http-equiv='refresh' content='2'>"
        "</head>"
        "<body>"
        "<h1>Pengasingan Sampah AI</h1>"
        "<p>Objek: " + webDetectedTrash + "</p>"
        "<p>Confidence: " + String(webConfidence, 1) + "%</p>"
        "<p>Status: " + webStatus + "</p>"
        "<p>Servo: " + String(webServoAngle) + " deg</p>"
        "</body>"
        "</html>";

    server.send(200, "text/html", html);
}



// =====================================================
// JSON STATUS
// =====================================================

void handleStatus()
{
    String json = "{";

    json += "\"trash\":\"";
    json += webDetectedTrash;
    json += "\",";

    json += "\"confidence\":";
    json += String(webConfidence, 1);
    json += ",";

    json += "\"status\":\"";
    json += webStatus;
    json += "\",";

    json += "\"servo_angle\":";
    json += String(webServoAngle);
    json += ",";

    json += "\"servo_status\":\"";
    json += webServoStatus;
    json += "\",";

    json += "\"ip\":\"";
    json += WiFi.localIP().toString();
    json += "\"";

    json += "}";

    server.send(
        200,
        "application/json",
        json
    );
}


// =====================================================
// FUNGSI PINTU ATAS
// =====================================================

void bukaDanTutupPintu()
{
    Serial.println(
        "[SERVO PINTU] Membuka pintu atas (90 darjah)..."
    );

    webServoStatus = "Pintu membuka";
    webStatus = "Sampah sedang dijatuhkan";

    servoPintuAtas.write(90);

    delay(1500);

    Serial.println(
        "[SERVO PINTU] Menutup semula pintu atas (0 darjah)..."
    );

    servoPintuAtas.write(0);

    webServoStatus = "Pintu menutup";

    delay(500);

    webServoStatus = "Sedia";
}


// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(115200);

    while (!Serial);

    // Servo
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);

    servoPintuAtas.setPeriodHertz(50);
    servoPusingan.setPeriodHertz(50);

    servoPintuAtas.attach(
        pinServoPintu,
        500,
        2400
    );

    servoPusingan.attach(
        pinServoPusing,
        500,
        2400
    );

    // Posisi asal
    servoPintuAtas.write(0);

    servoPusingan.write(45);

    webServoAngle = 45;
    webServoStatus = "Sedia";


    Serial.println(
        "=========================================="
    );

    Serial.println(
        "   Sistem Pengasingan Sampah Bermula!"
    );

    Serial.println(
        "=========================================="
    );


    // Kamera
    if (ei_camera_init() == false)
    {
        Serial.println(
            "[RALAT KAMERA] Gagal mulakan kamera!"
        );

        webStatus = "Kamera gagal";
    }
    else
    {
        Serial.println(
            "[STATUS KAMERA] Kamera berjaya dihidupkan."
        );

        webStatus = "Kamera sedia";
    }


    // WiFi
    setupWiFi();


    // Web server
    setupWebServer();


    Serial.println(
        "=========================================="
    );

    Serial.println(
        " SISTEM SIAP DIGUNAKAN"
    );

    Serial.println(
        "=========================================="
    );


    ei_sleep(2000);
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
    // Handle web request
    server.handleClient();


    if (ei_sleep(5) != EI_IMPULSE_OK)
    {
        return;
    }


    snapshot_buf =
        (uint8_t*)ps_malloc(
            EI_CAMERA_RAW_FRAME_BUFFER_COLS *
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS *
            EI_CAMERA_FRAME_BYTE_SIZE
        );


    if (snapshot_buf == nullptr)
    {
        Serial.println(
            "[RALAT MEMORI] Gagal alokasi memori PSRAM!"
        );

        webStatus = "Gagal alokasi PSRAM";

        return;
    }


    ei::signal_t signal;

    signal.total_length =
        EI_CLASSIFIER_INPUT_WIDTH *
        EI_CLASSIFIER_INPUT_HEIGHT;

    signal.get_data =
        &ei_camera_get_data;


    // Ambil gambar
    if (
        ei_camera_capture(
            (size_t)EI_CLASSIFIER_INPUT_WIDTH,
            (size_t)EI_CLASSIFIER_INPUT_HEIGHT,
            snapshot_buf
        ) == false
    )
    {
        Serial.println(
            "[RALAT KAMERA] Kamera gagal ambil gambar!"
        );

        webStatus = "Kamera gagal ambil gambar";

        free(snapshot_buf);

        return;
    }


    // Jalankan AI
    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR err =
        run_classifier(
            &signal,
            &result,
            debug_nn
        );


    if (err != EI_IMPULSE_OK)
    {
        Serial.printf(
            "[RALAT AI] Gagal menjalankan klasifikasi (Kod ralat: %d)\n",
            err
        );

        webStatus = "AI classifier gagal";

        free(snapshot_buf);

        return;
    }


    float highest_confidence = 0.0;

    String detected_trash = "tiada";


    Serial.println(
        "\n------------------------------------------"
    );

    Serial.println(
        "Bacaan Kamera Semasa:"
    );


    // Baca semua label
    for (
        uint16_t i = 0;
        i < EI_CLASSIFIER_LABEL_COUNT;
        i++
    )
    {
        float nilai =
            result.classification[i].value;


        Serial.printf(
            "  - %s: %.1f%%\r\n",
            ei_classifier_inferencing_categories[i],
            nilai * 100.0
        );


        if (nilai > highest_confidence)
        {
            highest_confidence = nilai;

            detected_trash =
                ei_classifier_inferencing_categories[i];
        }
    }


    // Update web status
    webDetectedTrash = detected_trash;

    webConfidence =
        highest_confidence * 100.0;

    lastDetectionTime = millis();


    // =================================================
    // LOGIK SERVO
    // =================================================

    if (highest_confidence > 0.50)
    {

        // -----------------------------
        // PLASTIC
        // -----------------------------

        if (
            detected_trash.equalsIgnoreCase("plastic") ||
            detected_trash.equalsIgnoreCase("plastik")
        )
        {

            Serial.printf(
                ">>> [STATUS KAMERA] PLASTIC %.1f%% <<<\n",
                highest_confidence * 100.0
            );


            Serial.println(
                "[TINDAKAN SERVO] "
                "Memusingkan pengasing ke PLASTIC"
            );


            webStatus = "Plastic dikesan";
            webServoStatus = "Ke zon PLASTIC";

            servoPusingan.write(0);

            webServoAngle = 0;


            delay(1000);


            bukaDanTutupPintu();
        }


        // -----------------------------
        // PAPER
        // -----------------------------

        else if (
            detected_trash.equalsIgnoreCase("paper") ||
            detected_trash.equalsIgnoreCase("kertas")
        )
        {

            Serial.printf(
                ">>> [STATUS KAMERA] PAPER %.1f%% <<<\n",
                highest_confidence * 100.0
            );


            Serial.println(
                "[TINDAKAN SERVO] "
                "Memusingkan pengasing ke PAPER"
            );


            webStatus = "Paper dikesan";
            webServoStatus = "Ke zon PAPER";

            servoPusingan.write(90);

            webServoAngle = 90;


            delay(1000);


            bukaDanTutupPintu();
        }


        // -----------------------------
        // GLASS
        // -----------------------------

        else if (
            detected_trash.equalsIgnoreCase("glass") ||
            detected_trash.equalsIgnoreCase("kaca")
        )
        {

            Serial.printf(
                ">>> [STATUS KAMERA] GLASS %.1f%% <<<\n",
                highest_confidence * 100.0
            );


            Serial.println(
                "[TINDAKAN SERVO] "
                "Memusingkan pengasing ke GLASS"
            );


            webStatus = "Glass dikesan";
            webServoStatus = "Ke zon GLASS";

            servoPusingan.write(180);

            webServoAngle = 180;


            delay(1000);


            bukaDanTutupPintu();
        }


        // -----------------------------
        // LABEL LAIN
        // -----------------------------

        else
        {

            Serial.printf(
                ">>> [STATUS KAMERA] "
                "Mengesan '%s' (%.1f%%), "
                "tiada servo digerakkan. <<<\n",

                detected_trash.c_str(),

                highest_confidence * 100.0
            );


            webStatus =
                "Objek lain dikesan";

            webServoStatus =
                "Tiada tindakan";
        }

    }

    else
    {

        Serial.printf(
            ">>> [AMARAN KAMERA] "
            "Objek TIDAK JELAS! "
            "'%s' (%.1f%%). <<<\n",

            detected_trash.c_str(),

            highest_confidence * 100.0
        );


        webStatus =
            "Objek tidak jelas";

        webServoStatus =
            "Sedia";
    }


    Serial.println(
        "------------------------------------------"
    );


    free(snapshot_buf);


    webStatus = "Menunggu objek...";

    delay(2000);
}


// =====================================================
// CAMERA INIT
// =====================================================

bool ei_camera_init(void)
{

    if (is_initialised)
        return true;


    esp_err_t err =
        esp_camera_init(&camera_config);


    if (err != ESP_OK)
    {

        Serial.printf(
            "Camera init failed with error 0x%x\n",
            err
        );

        return false;
    }


    sensor_t * s =
        esp_camera_sensor_get();


    if (s->id.PID == OV3660_PID)
    {

        s->set_vflip(s, 1);

        s->set_brightness(s, 1);

        s->set_saturation(s, 0);
    }


    s->set_vflip(s, 1);

    s->set_hmirror(s, 1);


    is_initialised = true;


    return true;
}


// =====================================================
// CAMERA DEINIT
// =====================================================

void ei_camera_deinit(void)
{

    esp_camera_deinit();

    is_initialised = false;
}


// =====================================================
// CAMERA CAPTURE
// =====================================================

bool ei_camera_capture(
    uint32_t img_width,
    uint32_t img_height,
    uint8_t *out_buf
)
{

    bool do_resize = false;


    if (!is_initialised)
        return false;


    camera_fb_t *fb =
        esp_camera_fb_get();


    if (!fb)
        return false;


    bool converted =
        fmt2rgb888(
            fb->buf,
            fb->len,
            PIXFORMAT_JPEG,
            snapshot_buf
        );


    esp_camera_fb_return(fb);


    if (!converted)
        return false;


    if (
        (img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS) ||
        (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS)
    )
    {
        do_resize = true;
    }


    if (do_resize)
    {

        ei::image::processing::crop_and_interpolate_rgb888(

            out_buf,

            EI_CAMERA_RAW_FRAME_BUFFER_COLS,
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS,

            out_buf,

            img_width,
            img_height
        );
    }


    return true;
}


// =====================================================
// EDGE IMPULSE GET DATA
// =====================================================

static int ei_camera_get_data(
    size_t offset,
    size_t length,
    float *out_ptr
)
{

    size_t pixel_ix =
        offset * 3;


    size_t pixels_left =
        length;


    size_t out_ptr_ix = 0;


    while (pixels_left != 0)
    {

        out_ptr[out_ptr_ix] =

            (snapshot_buf[pixel_ix + 2] << 16) +

            (snapshot_buf[pixel_ix + 1] << 8) +

            snapshot_buf[pixel_ix];


        out_ptr_ix++;

        pixel_ix += 3;

        pixels_left--;
    }


    return 0;
}
