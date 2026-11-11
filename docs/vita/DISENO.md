# sh2-vita — Diseño del port de Silent Hill 2 a PS Vita

Fecha: 2026-11-10. Proyecto personal. Basado en
[sh23ds](https://github.com/ankushKun/sh23ds) (GPL-3.0); referencia de Vita:
[SilentVitaHill](https://github.com/khajit2412/SilentVitaHill).

## Objetivo

Jugar Silent Hill 2 PC Director's Cut en una PS Vita con firmware modificado, de forma
que **se sienta como la versión de PC**: el juego a pantalla completa, un botón para el
mapa, otro para el inventario y los menús como en PC/PS2. Nada del diseño de dos
pantallas de la 3DS.

## Reglas

1. **Nada del juego en git.** Ni exe, ni archivos convertidos, ni salida del decompilador.
   El hook `tools/check_repo.sh` lo comprueba en cada commit.
2. **No reescribir la lógica del juego.** El código de `port/source/` se toca lo mínimo
   para poder seguir trayendo las mejoras de sh23ds (`git pull upstream main`).
3. Todo lo nuevo va en `vita/`.

## Arquitectura: capa de compatibilidad

El código del juego incluye `<3ds.h>`, `<citro3d.h>` y `<tex3ds.h>`. En la Vita esos
encabezados son nuestros (`vita/compat/`) y por dentro llaman a la Vita:

| Sistema 3DS | Uso en el juego | Implementación en Vita |
| --- | --- | --- |
| citro3d (GPU PICA200) | ~200 funciones y constantes | vitaGL (OpenGL ES 2). El estado fijo de la PICA (TexEnv, luces con LUT, niebla, proctex, alpha test) se traduce a **shaders GLSL generados según el estado** y guardados en caché (técnica de Citra/Azahar) |
| shaders `.pica` (world, skin, shadow, fog, movie, touchui) | 6 archivos | reescritos a mano en GLSL (`vita/shaders/`) |
| hid (botones, stick, táctil) | `hidKeys*`, `hidCircleRead`, `hidTouchRead` | `sceCtrl` + `sceTouch`, mapeados a los bits de la 3DS |
| ndsp (audio) | 24 canales con wavebufs | mezclador propio sobre SDL2 audio |
| y2r (conversión de video YUV) | videos MPEG-1 | shader YUV→RGB |
| svc / os / threads / LightLock / LightEvent | tiempo, hilos, bloqueos | SceKernel / SDL2 |
| linearAlloc, GSPGPU_Flush* | memoria compartida con la GPU | `malloc` / `vglMalloc`; los flush no hacen nada |
| fs / rutas `sdmc:/3ds/sh23ds` | lectura de datos | `ux0:data/sh2vita` |
| gfx / apt / cfgu / mcuHwc | pantalla, bucle de app, sistema | equivalentes en Vita o vacíos |

**Pantalla:** la superior de la 3DS (400×240) se dibuja a 960×544. La inferior no
existe: sus funciones se sustituyen en la fase 5 por menús al estilo PC.

**Texturas:** el conversor de sh23ds escribe ETC1 en mosaicos de 8×8, un formato que la
Vita no lee. Con `--target vita` escribe DXT1/DXT5 lineales (que la GPU de la Vita sí
lee). Mientras tanto, la capa decodifica ETC1 por CPU para no bloquear el avance.

## Estructura

```
port/source/       código del juego (sh23ds)
tools/sh2conv/     conversor (+ --target vita)
vita/compat/       capa de compatibilidad
vita/shaders/      shaders GLSL
vita/ui_pc/        menús al estilo PC (fase 5)
vita/sce_sys/      icono y LiveArea propios
vita/CMakeLists.txt, vita/build.sh
docs/vita/         este documento y notas
```

Compilación: WSL (Ubuntu) + VitaSDK + vitaGL + vitaShaRK + SDL2 → `sh2vita.vpk`
(Title ID `SHTW00001`). Datos en la Vita: `ux0:data/sh2vita/` (~1,8 GB).
Registro: `ux0:data/sh2vita/log.txt`.

## Fases y cuándo se dan por terminadas

| Fase | Qué | Terminada cuando, en la Vita real… |
| --- | --- | --- |
| 1 | Entorno + `.vpk` mínimo | se instala, abre, muestra una pantalla de color y escribe `log.txt` |
| 2 | Conversor para Vita | el pack se genera en el PC sin errores y el juego encuentra los archivos |
| 3 | Primera imagen | se ve una habitación con James moviéndose con el stick |
| 4 | Juego completo | se oye música y efectos, se ven los videos, se guarda y carga partida |
| 5 | Interfaz de PC | inventario, mapa, pausa, opciones y guardado a pantalla completa, como en PC |
| 6 | Pulido | 30 fps estables en las zonas pesadas (ciudad con niebla) y sin cierres en una partida completa |

## Riesgos

- **Imitar la GPU de la 3DS** (iluminación por tablas, niebla de gas, proctex) puede dar
  diferencias visuales. Se resuelve efecto por efecto, comparando con la 3DS/Azahar.
- **Memoria:** la Vita tiene más (256 MB + 128 MB de vídeo) que la New 3DS (124 MB), así
  que no se espera problema.
- **Compilar shaders en la consola** pausa la primera vez que aparece cada efecto. Para
  eso está la caché en `ux0:data/sh2vita/shader_cache/`.
