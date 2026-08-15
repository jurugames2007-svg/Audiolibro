from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
from xml.sax.saxutils import escape

out = Path(__file__).with_name('EVALUACION-VOZLARGA-10-10.xlsx')

sheets = {
'Resumen': [
 ['Módulo','Prueba','Objetivo 10/10','Resultado medido','Estado','Conclusión / evidencia'],
 ['Precisión','1.1 Voz clara','WER ≤ 0,5%; puntuación/capitalización perfectas','No ejecutado','NO EVALUADO','No se proporcionó audio de 10 min ni guion exacto. No es válido inventar un WER.'],
 ['Precisión','1.2 Vocabulario técnico','WER ≤ 1,0%; 100% términos','No ejecutado','NO EVALUADO','Faltan audio técnico y referencia revisada. VozLarga ahora acepta contexto/nombres, pero eso no prueba 100%.'],
 ['Precisión','1.3 Acentos','WER promedio ≤ 2,0% en 5 dialectos','No ejecutado','NO EVALUADO','Faltan los 5 audios y sus guiones. Whisper Small es multilingüe; no implica cumplir el umbral.'],
 ['Precisión','1.4 Reunión compleja','WER ≤ 3,0%; diarización 100%','No ejecutado','NO CUMPLE DEMOSTRADO','Solo existe diarización por canales estéreo. Separar 4-5 voces solapadas en el mismo canal exige otro modelo.'],
 ['Rendimiento','2.1 Archivo de 2 horas','RTF ≤ 0,15; GPU estable','RTF 0,1751 en prueba Tiny/CPU de 2 núcleos; GPU no medida','PARCIAL','Pipeline completo: 7200 s procesados en 1261 s, 9/9 bloques. No usa Small ni RX 5600 XT, por tanto no certifica producto final.'],
 ['Rendimiento','2.2 Cuatro instancias','4 × 30 min sin degradación desproporcionada','No ejecutado','NO EVALUADO','El sandbox no tiene RX 5600 XT. Ejecutar cuatro instancias puede agotar VRAM y no existe planificador GPU global.'],
 ['Robustez','3.1 Entradas MP3/M4A','Compatibilidad sin corrupción','MP3 7200,07 s y M4A 7200,00 s; 9/9 bloques cada uno','APROBADO','18/18 WAV PCM 16 kHz validados con filtros de mejora. Prueba repetible: npm run test:formats.'],
 ['Robustez','3.1 WAV/FLAC','Compatibilidad total','Aceptados por FFmpeg; prueba exhaustiva no ejecutada','PARCIAL','El selector y FFmpeg los admiten, pero este workbook no contiene medición aislada de dos horas para ambos.'],
 ['Robustez','3.1 Salidas','TXT, DOCX y SRT','Solo TXT UTF-8 consolidado','NO CUMPLE','DOCX y SRT no pertenecen al alcance implementado y no se deben marcar como aprobados.'],
 ['Calidad salida','3.2 Texto','Puntuación/capitalización impecables; párrafos lógicos','Párrafos deterministas; precisión lingüística no medida','PARCIAL','Se eliminan solapamientos y se forman párrafos. Sin referencia humana no se certifican gramática ni nombres propios.'],
 ['Arquitectura','Audio largo','Sin crash, timeout ni desborde','2 h MP3: 9/9 bloques, checkpoints y TXT final válidos','APROBADO','Watchdog Vulkan, respaldo CPU, WAV temporal por bloque y escritura final atómica.'],
 ['Entrada','Mejora de audio','Ruido/volumen robustos','Filtros + fallback + RMS/pico/clipping/silencio','APROBADO FUNCIONAL','No recupera muestras destruidas por clipping; informa el problema.'],
 ['Evaluación global','Calificación 10/10','Todos los umbrales aprobados','Evidencia insuficiente y salidas/diarización incompletas','NO CERTIFICADO','No corresponde afirmar 10/10 sin corpus de referencia y benchmark en RX 5600 XT.'],
],
'Evidencia': [
 ['Fecha','Prueba ejecutada','Entrada','Configuración','Resultado numérico','Resultado cualitativo'],
 ['2026-08-15','Sondeo y segmentación 2 h','MP3 sintético, 16 kHz mono, 7200,07 s','Bloques 900 s; paso 898 s; filtros de voz/ruido','9/9 bloques; 8×900 s + 1×16 s','APROBADO'],
 ['2026-08-15','Sondeo y segmentación 2 h','M4A/AAC sintético, 16 kHz mono, 7200,00 s','Bloques 900 s; paso 898 s; filtros de voz/ruido','9/9 bloques; 8×900 s + 1×16 s','APROBADO'],
 ['2026-08-15','Pipeline completo 2 h','MP3 sintético','whisper.cpp 1.9.2 CPU; Tiny F16; 2 hilos; VAD off','1261 s; RTF 0,1751; 9/9 TXT','APROBADO PARA FLUJO, NO PARA SMALL/GPU'],
 ['2026-08-15','Checkpoint y consolidación','9 bloques MP3','Checkpoint atómico por bloque','9 actualizaciones; TXT final comparado byte a byte','APROBADO'],
 ['2026-08-15','Regresión Silero CPU','30 s de silencio','Silero VAD 6.2.0 por CPU','>180 s; timeout de prueba','FALLÓ; mitigado: VAD solo Vulkan y off por defecto'],
 ['2026-08-15','FFmpeg formatos largos','18 bloques MP3/M4A','PCM s16le mono 16000 Hz','18/18 cabeceras, duración y formato válidos','APROBADO'],
 ['2026-08-15','Compilación Windows','VozLarga.exe x64','Zig/Clang, Win32, UCRT','PE x64; 912896 bytes','APROBADO ESTRUCTURAL'],
 ['2026-08-15','GPU RX 5600 XT','No disponible en sandbox','No ejecutado','Sin RTF/VRAM/temperatura','PENDIENTE EN HARDWARE REAL'],
],
'Casos requeridos': [
 ['ID','Audio requerido','Referencia requerida','Duración','Métrica','Objetivo','Estado actual'],
 ['1.1','Estudio, 1 hablante, español neutro','Guion exacto','10 min','WER','≤0,5%','FALTA AUDIO Y REFERENCIA'],
 ['1.2','Conferencia técnica','Transcripción experta','15 min','WER + términos','≤1,0%; 100% términos','FALTA AUDIO Y REFERENCIA'],
 ['1.3-MX','Español México','Guion exacto','5 min','WER','Promedio ≤2,0%','FALTA'],
 ['1.3-AR','Español Argentina','Guion exacto','5 min','WER','Promedio ≤2,0%','FALTA'],
 ['1.3-ES','Español España','Guion exacto','5 min','WER','Promedio ≤2,0%','FALTA'],
 ['1.3-CO','Español Colombia','Guion exacto','5 min','WER','Promedio ≤2,0%','FALTA'],
 ['1.3-CAR','Español Caribe','Guion exacto','5 min','WER','Promedio ≤2,0%','FALTA'],
 ['1.4','Reunión 4-5 voces y ruido','Transcripción manual con hablante','30 min','WER + atribución','≤3,0%; 100% atribución','FALTA; MISMO CANAL NO SOPORTADO'],
 ['2.1','Contenido complejo de 2 h','No necesaria para RTF','120 min','RTF/GPU/VRAM/temp','RTF ≤0,15','FALTA RX 5600 XT'],
 ['2.2','4 archivos distintos','No necesaria','4×30 min','RTF por instancia/VRAM','Sin degradación desproporcionada','FALTA RX 5600 XT'],
],
'Métricas': [
 ['Métrica','Fórmula','Método correcto','Advertencia'],
 ['WER','(Sustituciones + Eliminaciones + Inserciones) / Palabras de referencia × 100','Normalizar según protocolo fijado; conservar también puntuación para evaluación separada','No calcular con referencias generadas por el mismo modelo.'],
 ['RTF','Segundos de proceso / Segundos de audio','Cronómetro desde Iniciar hasta TXT verificado','RTF 0,15 = 18 min para 2 horas.'],
 ['Velocidad','Segundos de audio / Segundos de proceso','1 / RTF','RTF 0,15 equivale a 6,67× tiempo real.'],
 ['Diarización','Bloques correctamente atribuidos / bloques totales × 100','Referencia con identidad de hablante y límites temporales','Canal estéreo no equivale a diarización multihablante mono.'],
 ['Precisión términos','Términos correctos / términos de referencia × 100','Lista cerrada revisada por experto','El prompt puede ayudar, pero no garantiza 100%.'],
],
}

def col_letter(n):
    s=''
    while n:
        n, r = divmod(n-1,26); s=chr(65+r)+s
    return s

def cell_xml(value, row, col, header=False):
    ref=f'{col_letter(col)}{row}'
    style=' s="1"' if header else ''
    if isinstance(value,(int,float)):
        return f'<c r="{ref}"{style}><v>{value}</v></c>'
    text=escape(str(value))
    return f'<c r="{ref}" t="inlineStr"{style}><is><t xml:space="preserve">{text}</t></is></c>'

def sheet_xml(rows):
    widths=[14,26,30,32,20,65]
    cols=''.join(f'<col min="{i}" max="{i}" width="{w}" customWidth="1"/>' for i,w in enumerate(widths,1))
    data=[]
    for r,row in enumerate(rows,1):
        cells=''.join(cell_xml(v,r,c,r==1) for c,v in enumerate(row,1))
        data.append(f'<row r="{r}" ht="{30 if r==1 else 45}" customHeight="1">{cells}</row>')
    last_col=col_letter(max(len(r) for r in rows))
    return f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<sheetViews><sheetView workbookViewId="0"><pane ySplit="1" topLeftCell="A2" activePane="bottomLeft" state="frozen"/></sheetView></sheetViews>
<cols>{cols}</cols><sheetData>{''.join(data)}</sheetData>
<autoFilter ref="A1:{last_col}{len(rows)}"/><sheetFormatPr defaultRowHeight="15"/>
</worksheet>'''

content_types=['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>','<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">','<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>','<Default Extension="xml" ContentType="application/xml"/>','<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>','<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>']
for i in range(1,len(sheets)+1):
    content_types.append(f'<Override PartName="/xl/worksheets/sheet{i}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>')
content_types.append('</Types>')

workbook_sheets=''.join(f'<sheet name="{escape(name)}" sheetId="{i}" r:id="rId{i}"/>' for i,name in enumerate(sheets,1))
workbook=f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets>{workbook_sheets}</sheets></workbook>'''
rels=''.join(f'<Relationship Id="rId{i}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet{i}.xml"/>' for i in range(1,len(sheets)+1))
rels += f'<Relationship Id="rId{len(sheets)+1}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>'
workbook_rels=f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">{rels}</Relationships>'''
root_rels='''<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>'''
styles='''<?xml version="1.0" encoding="UTF-8" standalone="yes"?><styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><fonts count="2"><font><sz val="11"/><name val="Calibri"/></font><font><b/><color rgb="FFFFFFFF"/><sz val="11"/><name val="Calibri"/></font></fonts><fills count="3"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FF1F4E78"/><bgColor indexed="64"/></patternFill></fill></fills><borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"><alignment vertical="top" wrapText="1"/></xf><xf numFmtId="0" fontId="1" fillId="2" borderId="0" xfId="0"><alignment vertical="center" wrapText="1"/></xf></cellXfs></styleSheet>'''

with ZipFile(out,'w',ZIP_DEFLATED) as z:
    z.writestr('[Content_Types].xml',''.join(content_types))
    z.writestr('_rels/.rels',root_rels)
    z.writestr('xl/workbook.xml',workbook)
    z.writestr('xl/_rels/workbook.xml.rels',workbook_rels)
    z.writestr('xl/styles.xml',styles)
    for i,(name,rows) in enumerate(sheets.items(),1):
        z.writestr(f'xl/worksheets/sheet{i}.xml',sheet_xml(rows))
print(out, out.stat().st_size)
