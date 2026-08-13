//////////////////////////////////////////////////////////////////
//
//                  oled_display.hpp
//
// Descripción: Define la clase `OledDisplay`, una abstracción del
//              display OLED SSD1306 de 128x32 píxeles conectado
//              por I2C (dirección 0x3C) usando la librería
//              bcm2835 y SSD1306_OLED_RPI.
//
//              La clase es "tolerante a fallos": si no hay display
//              disponible (falta hardware o root) la aplicación
//              puede seguir funcionando en modo solo consola.
//
// Autor: lion
// Fecha: 2024 Octb (refactorizado)
//
//////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>

class SSD1306;

namespace OLED {

// Controlador de pantalla OLED 128x32 (SSD1306, I2C).
class OledDisplay {
public:
    static constexpr int kWidth = 128;
    static constexpr int kHeight = 32;
    static constexpr int kRows = kHeight / 8;      // 4 filas de 8 píxeles
    static constexpr int kMaxNameChars = kWidth / 5; // 25 caracteres con fuente 5x8
    // Marquee (en fotogramas de 250 ms): pausa inicial antes de mover y
    // avance lento para que el texto sea legible.
    static constexpr int kMarqueeDelayFrames = 6; // 1.5 s quieto
    static constexpr int kMarqueeStepFrames = 4;  // 1 carácter / segundo

    OledDisplay();
    ~OledDisplay();

    // Inicializa bcm2835 y el display. Devuelve false si no hay display.
    bool init();

    bool ready() const { return ready_; }

    void clear();

    // Pantalla principal del reproductor:
    //   fila 0 -> estado (reproduciendo/pausa) + "03/12" + volumen
    //   fila 1 -> nombre del tema (con desplazamiento si es largo)
    //   fila 2 -> reproduciendo: barra de progreso; parado: ayuda de teclas
    //   fila 3 -> reproduciendo: tiempo "0:42/2:15" + versión; parado: versión
    // `volumePercent` >= 0 muestra el volumen; negativo lo omite.
    // `version` es la versión de la app ("1.7.4"); `positionSeconds` y
    // `durationSeconds` alimentan la barra y el tiempo (0 = desconocido).
    void showTrack(int index, int total,
                   const std::string& name,
                   bool playing, bool paused,
                   int scrollOffset, int volumePercent = -1,
                   const std::string& version = std::string(),
                   double positionSeconds = 0.0,
                   double durationSeconds = 0.0);

    // Mensaje de dos líneas (usado para arranque / estado BT).
    void showMessage(const std::string& line1, const std::string& line2 = "");

    void shutdown();

private:
    void drawLine(int y, const std::string& text);

    SSD1306* oled_;
    bool ready_ = false;
    std::uint8_t buffer_[kWidth * (kHeight / 8)];
};

} // namespace OLED
