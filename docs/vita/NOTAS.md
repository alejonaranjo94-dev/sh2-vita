# Notas técnicas del port a Vita

Lo aprendido de otros ports con vitaGL (sobre todo SilentVitaHill, `pc_port/vita_port/HANDOFF.md`)
y lo que vamos descubriendo. Léelo antes de tocar `vita/compat/`.

## vitaGL y shaders

- Los shaders van en **GLSL ES 1.00**. vitaGL los traduce a Cg con vitaShaRK y los compila en la
  consola (hace falta `libshacccg.suprx` en `ur0:data/`).
- vitaGL compila dentro de `glLinkProgram` y **se cuelga si un shader no compila**. El motivo solo
  aparece en el log de vitaShaRK: instalar `shark_install_log_cb` y pasar sus líneas a `log.txt`.
- El traductor asigna un `TEXCOORDn` a cada varying y **falla con más de 10**. Agrupar varyings.
- Palabras reservadas de Cg que no pueden usarse como nombres en GLSL: `major` (y en general las de
  Cg/HLSL: `half`, `fixed`, `sampler`, `texture`, `in`/`out` como nombres…).
- No hay `textureSize`, `textureLod` ni `gl_VertexID`.
- Un `discard` en el shader desactiva la eliminación de superficies ocultas de la GPU (PowerVR):
  generar variantes sin `discard` cuando la prueba alfa está apagada.
- No existe `glBlendColor` (GXM no tiene color constante de mezcla): emularlo en el shader.

## Memoria y rendimiento

- `ATTRIBUTE2=12` en el `param.sfo` pide el modo de memoria ampliada.
- `_newlib_heap_size_user` fija el heap de C; vitaGL usa el resto de la RAM menos el umbral que se
  le da en `vglInitExtended`. Ajustar mirando la memoria libre en el log.
- vitaGL **reserva memoria en cada `glBufferData`**: subir solo los vértices usados, o usar
  `vglAllocFromScratch`. Un buffer grande por frame costó ~290 ms/frame en SilentVitaHill.
- **Nunca esperar en bucle activo** (busy-spin): roba la CPU al hilo de pantalla de GXM. Dormir
  (`sceKernelDelayThread`).
- Varios render targets por frame: `vglSetupRenderTargetScenesNum(4, 4)` antes de `vglInitExtended`,
  o cada pasada espera un vblank.
- Filtrar llamadas GL redundantes (uniforms, binds, viewport): cada una cuesta en la Vita.
- `vglWaitVblankStart(GL_FALSE)` al iniciar: el juego marca su propio ritmo.

## Compilador

- `arm-vita-eabi` usa **enums cortos** (`sizeof(enum) == 1` si cabe), igual que devkitARM de la
  3DS, así que el código del juego se compila con el comportamiento por defecto. Ojo con structs
  propios que mezclen enums y datos binarios.
- El `char` es sin signo en ARM (igual en 3DS y Vita).
- `-Wl,--defsym,__sce_headroom=0x2000` evita el error "segment 1 overlaps" de `vita-elf-create`.

## Windows

- **Smart App Control** está activo en el PC de desarrollo y bloquea el compilador de VitaSDK para
  Windows. No desactivarlo (no se puede volver a activar sin reinstalar Windows). Compilar en
  GitHub Actions (`.github/workflows/vita.yml`) o en WSL.

## Decisiones de la capa de compatibilidad

- Los render targets conservan la orientación vertical de la 3DS (240×400). Las matrices "Tilt"
  giran la imagen como en la 3DS y `gfx.c` la gira de vuelta al mostrarla a 960×544.
- Profundidad: la 3DS usa z de clip en −w..0 y `C3D_DepthMap(scale, offset)`. El vertex shader
  calcula `gl_Position.z = 2·(scale·z + offset·w) − w`.
- Los uniforms del juego se escriben en un banco de 96 registros (`C3D_FVUnif`), como en la 3DS, y
  cada programa GLSL lee los que necesita.
- y2r (conversión YUV de los vídeos) no existe: `y2rInit` falla y `movie.c` usa su ruta por CPU.
