# Justificacion Tecnica del Pipeline

Clase: **Sistemas Operativos - C2661-SI2004-5186**  
Profesor: **Edison Valencia**  
Desarrolladores: **Maximiliano Bustamante** y **Valeria Hornung**

## Objetivo

Comparar dos arquitecturas de pipeline en memoria RAM para archivos de 50 MB:

- Pipeline correcto: **Comprimir -> Encriptar**.
- Pipeline alterno (para evidencia): **Encriptar -> Comprimir**.

El criterio de decision es equilibrar:

- Espacio de I/O (tamano transmitido/escrito).
- Tiempo de CPU.
- Tiempo total (wall-clock).
- Integridad y seguridad de llaves en RAM.

## Metodo implementado

### Transformaciones

- **Compresion:** RLE en memoria.
- **Cifrado simetrico:** XTEA en modo CBC con padding PKCS#7.
- **Pipeline en RAM:** no se usa cifrado/escritura de alto nivel a disco intermedio.

### Seguridad de llave

- La llave se recibe por consola (o `PIPELINE_KEY` para automatizar benchmark).
- Nunca se recibe por argumentos `argv` para evitar fuga en historial/procesos.
- Se intenta bloquear pagina con `VirtualLock`/`mlock` para reducir riesgo de swap.
- Luego de derivar la llave interna, la passphrase se borra de RAM con `secure_zero`.

## Metricas obtenidas

Fuente: `docs/metrics.csv` (promedio de 5 corridas por escenario).

- **A_classic_copy:** input 50.00 MB, output 50.00 MB, wall 5.654 ms, CPU 5.8 ms.
- **B_compress_only:** input 50.00 MB, output 0.434 MB, wall 19.394 ms, CPU 19.4 ms.
- **C_compress_then_encrypt:** input 50.00 MB, output 0.434 MB, wall 22.988 ms, CPU 23.0 ms.
- **D_encrypt_then_compress:** input 50.00 MB, output 99.609 MB, wall 496.424 ms, CPU 496.6 ms.

Indicadores clave:

- Reduccion de tamano con `B` frente a `A`: **99.131%**.
- Reduccion de tamano con `C` frente a `A`: **99.131%** (conserva beneficio de compresion aun tras cifrar).
- Crecimiento de tamano con `D` frente a `A`: **99.217%** (casi duplica el archivo).
- Sobrecosto temporal de `C` frente a `B`: **18.53%** (costo de seguridad agregado).
- `D` tarda **21.59x** mas que `C` en tiempo total.

## Graficas

### Tamano transmitido

![Tamano transmitido](docs/grafica_tamano.png)

### CPU vs espera de I/O estimada

![CPU e I/O](docs/grafica_tiempos.png)

### Tiempo total (wall-clock)

![Wall clock](docs/grafica_wall.png)

## Analisis: por que gana Comprimir -> Encriptar

- La compresion necesita patrones repetitivos para reducir bytes.
- La encriptacion produce salida pseudoaleatoria (alta entropia).
- Si se encripta primero, se destruyen patrones; compresion posterior deja de ser efectiva.
- Por eso `encrypt -> compress` no reduce tamano y puede inflarlo por overhead del formato/padding.
- En cambio `compress -> encrypt` primero reduce datos y luego asegura confidencialidad, manteniendo ventaja de espacio con un costo de CPU controlado.

Nota de interpretacion: en estas mediciones la diferencia `wall - cpu` es casi nula por el entorno de prueba (archivo en almacenamiento local rapido y tiempos cortos). Aun asi, la comparacion relativa entre escenarios sigue siendo valida para justificar el orden del pipeline.

## Respuestas de sustentacion (preguntas trampa)

### 1) "Si encripto primero y luego comprimo, que pasa?"

El archivo no se comprime de forma util. La salida cifrada tiene alta entropia y casi no presenta patrones repetitivos, por lo que la compresion resulta ineficiente o expansiva.

### 2) "Si borran la llave, puede quedar en swap?"

Si el SO pagina esa memoria antes del borrado, existe riesgo. Para mitigarlo se usa `VirtualLock`/`mlock` para intentar fijar la pagina en RAM y evitar swap durante el uso de la passphrase.

### 3) "Por que usar buffers de 4096 bytes?"

Porque 4096 bytes coincide con el tamano de pagina comun en arquitecturas x86/x64 y bloque frecuente de FS; alinear a este orden suele reducir operaciones parciales de I/O y mejora eficiencia del bus.

## Conclusion final

Se implementaron y probaron ambas rutas.  
La arquitectura recomendada es **Comprimir -> Encriptar** porque:

- mantiene confidencialidad de datos en reposo;
- conserva el ahorro de espacio de I/O;
- evita el error de aplicar compresion sobre datos cifrados de alta entropia.

La ruta inversa se deja implementada como evidencia experimental de por que falla arquitectonicamente.
