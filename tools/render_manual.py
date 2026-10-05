#!/usr/bin/env python3
"""Render the checked-in user manual; requires ReportLab, no network or hardware."""
from pathlib import Path
import html
import re
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, PageBreak, Table, TableStyle, Preformatted

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'output/pdf/Bonsai-8-User-Manual.pdf'
INK = colors.HexColor('#333b32')
ACCENT = colors.HexColor('#536c40')
PAPER = colors.HexColor('#fafaf5')


def inline(text):
    text = html.escape(text)
    text = re.sub(r'\[([^\]]+)\]\((https?://[^)]+)\)', r'<link href="\2" color="#536c40"><u>\1</u></link>', text)
    text = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', text)
    text = re.sub(r'`([^`]+)`', r'<font name="Courier" size="8.3">\1</font>', text)
    return text


def footer(canvas, doc):
    canvas.saveState()
    canvas.setFillColor(PAPER)
    canvas.rect(0, 0, doc.pagesize[0], doc.pagesize[1], fill=1, stroke=0)
    canvas.setFillColor(INK)
    canvas.setFont('Helvetica', 8)
    canvas.drawString(20*mm, 15*mm, 'BONSAI 8  /  USER MANUAL')
    canvas.drawRightString(doc.pagesize[0]-20*mm, 15*mm, str(doc.page))
    canvas.setStrokeColor(colors.HexColor('#d7ddcf'))
    canvas.line(20*mm, 21*mm, doc.pagesize[0]-20*mm, 21*mm)
    canvas.restoreState()


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    styles = getSampleStyleSheet()
    styles.add(ParagraphStyle(name='ManualBody', fontName='Helvetica', fontSize=10,
                             leading=14, textColor=INK, spaceAfter=7))
    styles.add(ParagraphStyle(name='ManualTitle', fontName='Helvetica-Bold', fontSize=37,
                             leading=43, textColor=INK, spaceBefore=36, spaceAfter=23))
    styles.add(ParagraphStyle(name='ManualSection', fontName='Helvetica-Bold', fontSize=23,
                             leading=28, textColor=INK, spaceAfter=17, keepWithNext=True))
    styles.add(ParagraphStyle(name='ManualSub', fontName='Helvetica-Bold', fontSize=13,
                             leading=18, textColor=ACCENT, spaceBefore=12, spaceAfter=8, keepWithNext=True))
    styles.add(ParagraphStyle(name='ManualCell', fontName='Helvetica', fontSize=9.1,
                             leading=12.8, textColor=INK, spaceAfter=0))
    styles.add(ParagraphStyle(name='ManualCode', fontName='Courier', fontSize=8.0,
                             leading=12, textColor=INK, backColor=colors.HexColor('#edf0e7'),
                             borderPadding=9, spaceBefore=4, spaceAfter=12))
    story = []
    lines = (ROOT / 'USER_MANUAL.md').read_text().splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        if line.startswith('# '):
            story.append(Paragraph(inline(line[2:]), styles['ManualTitle']))
        elif line.startswith('## '):
            story.extend([PageBreak(), Paragraph(inline(line[3:]), styles['ManualSection'])])
        elif line.startswith('### '):
            if line[4:] in ('Record the mix without a computer', 'Four small side status LEDs',
                            'Match tempo, key and individual stem pitch'):
                story.append(PageBreak())
            story.append(Paragraph(inline(line[4:]), styles['ManualSub']))
        elif line.startswith('```'):
            code = []
            i += 1
            while i < len(lines) and not lines[i].startswith('```'):
                code.append(lines[i])
                i += 1
            story.append(Preformatted('\n'.join(code), styles['ManualCode']))
        elif line.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                cells = [x.strip() for x in lines[i].strip('|').split('|')]
                if not all(re.fullmatch(r':?-+:?', x) for x in cells):
                    rows.append([Paragraph(inline(x), styles['ManualCell']) for x in cells])
                i += 1
            i -= 1
            table = Table(rows, colWidths=[57*mm, 113*mm], repeatRows=1, hAlign='LEFT')
            table.setStyle(TableStyle([
                ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#e0e7d7')),
                ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.HexColor('#f1f3ed'), PAPER]),
                ('VALIGN', (0, 0), (-1, -1), 'TOP'),
                ('LEFTPADDING', (0, 0), (-1, -1), 9),
                ('RIGHTPADDING', (0, 0), (-1, -1), 9),
                ('TOPPADDING', (0, 0), (-1, -1), 3),
                ('BOTTOMPADDING', (0, 0), (-1, -1), 3),
                ('LINEBELOW', (0, 0), (-1, 0), 0.6, colors.HexColor('#bcc9ad')),
            ]))
            story.extend([table, Spacer(1, 12)])
        else:
            story.append(Paragraph(inline(line), styles['ManualBody']))
        i += 1
    doc = SimpleDocTemplate(str(OUT), pagesize=(210*mm, 297*mm), rightMargin=20*mm,
                            leftMargin=20*mm, topMargin=20*mm, bottomMargin=25*mm,
                            title='Bonsai 8 - User Manual', author='Bonsai 8 project',
                            subject='Physical controls, two-song stem mixing, website and firmware limits')
    doc.build(story, onFirstPage=footer, onLaterPages=footer)
    print(OUT)


if __name__ == '__main__':
    main()
