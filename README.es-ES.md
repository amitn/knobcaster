

# ESP32-S3 Cast Knob

[![CI](https://github.com/amitn/knobcaster/actions/workflows/ci.yml/badge.svg)](https://github.com/amitn/knobcaster/actions/workflows/ci.yml)
[![Flash in browser](https://img.shields.io/badge/flash%20in%20browser-WebSerial-1f6feb?logo=googlechrome&logoColor=white)](https://amitn.github.io/knobcaster/)

> Un mando giratorio de escritorio que controla todos los altavoces Google Cast / Chromecast en tu red Wi-Fi:
> gira para ajustar el volumen, presiona para seleccionar un altavoz, con información de reproducción actual, carátulas de álbum y
> controles de reproducción en una pantalla táctil circular. ESP32-S3 + ESP-IDF + LVGL.

Un controlador físico de volumen y reproducción para **altavoces Google Cast (Chromecast)**,
construido sobre la placa [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8).

Gira la perilla para cambiar el volumen. Presiónala para elegir un altavoz, mantén presionada para silenciar.
Los botones en pantalla gestionan reproducir / pausar / siguiente / anterior, y la pantalla circular
muestra lo que se está reproduciendo: título, artista y la portada del álbum como fondo atenuado.

<p align="center">
  <img src="docs/now-playing.png" alt="Now-playing screen: speaker name, title/artist, album art behind the text, and the volume ring" width="320">
</p>
<p align="center"><em>perilla = volumen · presionar = lista de altavoces · mantener presionado = silenciar · deslizar = cambiar altavoz</em></p>

## Status

✅ **Firmware funcional.** Descubre y controla altavoces Cast de extremo a extremo:
reproducción actual, volumen/silencio, transporte, cambio de altavoz, grupos, carátulas de álbum y
configuración de Wi-Fi. Consulta [Características](#features) y el [mapa de ruta](docs/06-roadmap.md).

## Features

- **Volumen y silencio** — gira la perilla (un anillo de color específico para cada altavoz lo sigue);
  mantén presionada para silenciar.
- **Cambiar de altavoz** — presiona la perilla (o toca el nombre) para ver una lista desplazable,
  o desliza izquierda/derecha. Los altavoces usados recientemente permanecen activos, por lo que volver a ellos es
  instantáneo.
- **Reproducción actual** — título, artista, estado de reproducción y la **portada del álbum** mostrada como
  fondo atenuado. Los títulos no latinos (p. ej., hebreo, derecha a izquierda) se renderizan correctamente.
- **Controles de reproducción** — botones en pantalla de anterior / reproducir-pausar / siguiente, habilitados según lo que soporte la aplicación.
- **Grupos Cast** — los grupos multihabitación se descubren y son controlables.
- **Configuración de Wi-Fi en el dispositivo** — código QR + portal cautivo, sin recompilación (ver más abajo).
- **Programación y actualización sin cadena de herramientas** — la primera programación desde el navegador vía
  [WebSerial](https://amitn.github.io/knobcaster/); después, la perilla se actualiza
  por sí misma **por aire (OTA)** desde las versiones de GitHub.

## Quick start

### Flash from your browser (no toolchain) ⚡

La forma más rápida de poner en marcha la placa: sin `uv`, PlatformIO ni `esptool`:

1. Abre el **[programador web → amitn.github.io/knobcaster](https://amitn.github.io/knobcaster/)**
   en **Chrome o Edge en escritorio** (WebSerial no es compatible con Firefox/Safari).
2. Conecta la perilla al USB-C (el **lado del ESP32-S3** — aparecerá como un puerto serie).
3. Haz clic en **Install**, selecciona el puerto y deja que programe y borre la memoria.

Programa la última [versión](https://github.com/amitn/knobcaster/releases) vía
WebSerial; después, la perilla se actualiza por sí misma **por aire (OTA)**, por lo que nunca necesitarás un
cable de nuevo. ¿Prefieres compilarlo tú mismo? Sigue leyendo.

### Build from source

Prerrequisitos: [`uv`](https://docs.astral.sh/uv/) y [`just`](https://github.com/casey/just).
El propio PlatformIO se instala *dentro de un entorno virtual gestionado por uv* — **no** necesitas un
PlatformIO global.

```bash
just setup          # create .venv via uv, install pinned PlatformIO into it
just doctor         # (optional) check toolchain + detected serial ports
just build          # compile the firmware
just flash          # build + upload to the board (USB-C, S3 side — see below)
just monitor        # watch serial logs
just dev            # build + upload + monitor in one shot
```

Ejecuta `just` sin argumentos para listar todas las recetas. La primera compilación descarga la
cadena de herramientas ESP-IDF y los componentes gestionados, por lo que toma unos minutos; las compilaciones posteriores
toman segundos.

Al encenderse por primera vez, la perilla arranca, se conecta al Wi-Fi (o inicia el portal de configuración — ver
más abajo), descubre tus altavoces Cast vía mDNS y muestra la pantalla de reproducción actual del primer altavoz. Luego úsalo como se describe en la [Guía de usuario](#user-guide).

### Wi-Fi setup (on-device, no rebuild)

En el primer arranque (sin red guardada) la pantalla muestra un **código QR**. Escanéalo para unirte
a la red de configuración `CastKnob-XXXX` del dispositivo, luego se abrirá un formulario web (en `192.168.4.1`);
introduce el nombre y contraseña de tu Wi-Fi doméstico y haz clic en **Guardar y Conectar**. Las credenciales
se almacenan en la NVS y se reutilizan en cada arranque; un icono de Wi-Fi muestra el estado de conexión.
(Para desarrollo, también puedes ejecutar `cp include/secrets.h.example include/secrets.h`
y compilar las credenciales directamente en el firmware.)

### Flashing note — dual-MCU USB

La placa multiplexa un único USB-C entre el **ESP32-S3** (USB nativo, `/dev/ttyACM*`)
y un **ESP32** secundario (puente CH340, `/dev/ttyUSB*`). `platformio.ini` asigna
las cargas de firmware al lado `ttyACM*` (S3). Si la programación se dirige al chip incorrecto, desconecta y vuelve a conectar el USB o
ingresa al modo de descarga (mantén presionado **BOOT**, presiona brevemente **RESET**, suelta **BOOT**).

## User guide

La pantalla circular es la vista de reproducción actual para el **altavoz activo**. Todas estas
acciones funcionan desde ahí:

| Acción | Realiza esto |
|--------|---------|
| **Cambiar volumen** | Gira la perilla. El anillo de color muestra el nivel y se actualiza al instante; el altavoz lo sigue. |
| **Silenciar / cancelar silencio** | Mantén presionada la perilla (el anillo se vuelve rojo mientras está silenciado). |
| **Elegir un altavoz** | Presiona la perilla o toca el nombre del altavoz. Aparece una lista: **gira** la perilla para desplazarte, **presiona** para seleccionar (o toca una fila). Toca fuera o espera para descartar. |
| **Siguiente / altavoz anterior** | Desliza el dedo hacia la izquierda / derecha en la pantalla. |
| **Reproducir / pausar, siguiente, anterior** | Toca los botones en pantalla. Los botones se atenúan cuando la aplicación no los soporta. |

Notas:

- **Retorno instantáneo.** Los pocos altavoces usados más recientemente permanecen conectados, por lo que
  volver a uno es inmediato (sin reconexión). Un altavoz completamente nuevo muestra
  "conectando…" durante ~½ segundo.
- **Color por altavoz.** Cada altavoz obtiene su propio color para el anillo de volumen, por lo que puedes
  distinguir de un vistazo en cuál te encuentras.
- **Portada y títulos.** La carátula se carga en segundo plano (un momento después
  de la canción) y se atenúa detrás del texto. Los títulos en cualquier alfabeto, incluido
  el de derecha a izquierda (hebreo), se renderizan correctamente.
- **Actualizaciones en vivo.** Si otra persona cambia el volumen u omite una canción, la
  pantalla lo refleja.
- **Grupos.** Los grupos multihabitación de Cast aparecen en la lista como cualquier altavoz; la
  perilla controla el volumen del grupo.

## How it works (one paragraph)

El ESP32-S3 se une a tu red Wi-Fi y luego usa **mDNS** para descubrir servicios `_googlecast._tcp`
en la red local. Para un altavoz seleccionado, abre un socket **TLS** en el puerto
`8009` y se comunica mediante el protocolo **CASTV2** (marcos protobuf con prefijo de longitud cuyas
cargas útiles son JSON) para leer el estado de medios/volumen y enviar comandos de transporte. La
perilla, la pantalla táctil y una pequeña interfaz gráfica **LVGL** forman la superficie de control. Consulta
[docs/02-architecture.md](docs/02-architecture.md).

## Documentation

| Documento | Contenido |
|-----|--------------|
| [00 — Descripción general y objetivos](docs/00-overview.md) | Alcance, MVP, fuera del alcance |
| [01 — Referencia de hardware](docs/01-hardware.md) | Placa, ICs, mapa de pines GPIO |
| [02 — Arquitectura del firmware](docs/02-architecture.md) | Tareas, módulos, flujo de datos |
| [03 — Protocolo Cast](docs/03-cast-protocol.md) | mDNS, CASTV2, espacios de nombres, flujos de mensajes |
| [04 — Interfaz de usuario / UX](docs/04-ui-ux.md) | Modelo de interacción perilla + táctil, pantallas LVGL |
| [05 — Compilación y herramientas](docs/05-build-and-tooling.md) | Flujo de trabajo uv + Just + PlatformIO |
| [06 — Mapa de ruta](docs/06-roadmap.md) | Hitos y preguntas abiertas |
| [07 — Pruebas](docs/07-testing.md) | Captura de trazas + pruebas unitarias nativas (planificadas) |

## Toolchain

- **[uv](https://docs.astral.sh/uv/)** — gestiona el entorno virtual de Python donde se ejecuta PlatformIO.
- **[just](https://github.com/casey/just)** — ejecutor de tareas (`just setup`, `just flash`, …).
- **[PlatformIO](https://platformio.org/)** — cadena de herramientas de compilación/programación (framework **ESP-IDF**).
- **[ESP-IDF](https://docs.espressif.com/projects/esp-idf/)** — `esp-tls`, `mdns`, `cJSON`, controladores.
- **[LVGL 9](https://lvgl.io/)** — interfaz gráfica embebida (vía `esp_lvgl_port`).

## Prior art / references

No empezamos de cero: estos proyectos informan el diseño (consulta
[docs/02-architecture.md](docs/02-architecture.md#prior-art--what-we-borrow)):

- **[ESPCaster](https://github.com/amitn/ESPCaster)** — stack del mismo autor en ESP-IDF
  para descubrimiento + control de Chromecast + LVGL (en una placa diferente). Referencia
  principal para la capa Cast.
- **[BSP de EmbeddedWizard](https://github.com/EmbeddedWizardGUI/ESP32-S3-Knob-Touch-LCD-1.8-EN)**
  — BSP de ESP-IDF para *esta placa exacta*; fuente del mapa de pines confirmado.
- **[BlueKnob](https://github.com/joshuacant/BlueKnob)** — control remoto multimedia BLE en
  esta placa; fuente del controlador de pantalla **SH8601** + componentes BSP (pantalla táctil,
  PWM de retroiluminación, codificador).
- **[roon-knob](https://github.com/muness/roon-knob)** — controlador de Roon en esta
  placa (análogo cercano: transporte multimedia + volumen por red).
- **[Ejemplos de placa de desarrollo de ihayri](https://github.com/ihayri/ESP32-S3-1.8inch-Knob-Display-Development-Board)**
  y **[VolosR/Knob18Meters](https://github.com/VolosR/Knob18Meters)** — más
  ejemplos de pantalla/UI para esta placa exacta.
