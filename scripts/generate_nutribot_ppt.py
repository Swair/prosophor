#!/usr/bin/env python3
# Copyright 2026 Prosophor Contributors
# SPDX-License-Identifier: Apache-2.0
"""Generate the NutriBot project-introduction deck (16:9, Chinese).

Run:  python3 scripts/generate_nutribot_ppt.py
Out:  docs/nutrition_robot/NutriBot_项目介绍_路演版.pptx
"""

from __future__ import annotations

import os
from pathlib import Path

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.dml import MSO_LINE
from pptx.enum.shapes import MSO_CONNECTOR, MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, MSO_AUTO_SIZE, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Inches, Pt
from lxml import etree


# ---------------------------------------------------------------------------
# Design tokens
# ---------------------------------------------------------------------------

SW, SH = 13.333, 7.5
M = 0.72
CW = SW - 2 * M

INK = "102A33"
INK_SOFT = "334B52"
MUTED = "5F7378"
TEAL = "0E7C7B"
TEAL_BRIGHT = "14B8A6"
MINT = "E6F4F1"
MINT_2 = "CFEAE4"
AMBER = "D98A3D"
AMBER_SOFT = "FBEFE2"
CORAL = "C9655A"
CORAL_SOFT = "F8E8E5"
BG = "F7F9F8"
CARD = "FFFFFF"
LINE = "DCE7E4"
DARK = "0B2026"
DARK_2 = "12343B"
DARK_3 = "1A444A"
WHITE = "FFFFFF"

CN = "Microsoft YaHei"
NUM = "Arial"
MONO = "Consolas"


def rgb(value: str) -> RGBColor:
    return RGBColor.from_string(value)


def _set_run(run, size=12, color=INK, bold=False, font=CN, italic=False):
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.italic = italic
    run.font.color.rgb = rgb(color)
    run.font.name = font

    # East-Asian / complex-script typefaces: python-pptx only writes a:latin.
    rpr = run._r.get_or_add_rPr()
    latin = rpr.find(qn("a:latin"))
    ea = rpr.find(qn("a:ea"))
    if ea is None:
        ea = etree.SubElement(rpr, qn("a:ea"))
        if latin is not None:
            latin.addnext(ea)
    ea.set("typeface", font)
    cs = rpr.find(qn("a:cs"))
    if cs is None:
        cs = etree.SubElement(rpr, qn("a:cs"))
        if ea is not None:
            ea.addnext(cs)
    cs.set("typeface", font)


def add_text(
    slide,
    x,
    y,
    w,
    h,
    text,
    size=14,
    color=INK,
    bold=False,
    align=PP_ALIGN.LEFT,
    anchor=MSO_ANCHOR.TOP,
    font=CN,
    line_spacing=1.06,
    space_after=0,
    rotation=0,
):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.word_wrap = True
    tf.auto_size = MSO_AUTO_SIZE.NONE
    tf.margin_left = 0
    tf.margin_right = 0
    tf.margin_top = 0
    tf.margin_bottom = 0
    tf.vertical_anchor = anchor
    if rotation:
        box.rotation = rotation

    lines = str(text).split("\n")
    for i, line in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        p.line_spacing = line_spacing
        p.space_after = Pt(space_after)
        run = p.add_run()
        run.text = line
        _set_run(run, size, color, bold, font)
    return box


def add_block(
    slide,
    x,
    y,
    w,
    h,
    paragraphs,
    anchor=MSO_ANCHOR.TOP,
    margins=(0, 0, 0, 0),
):
    """paragraphs: list of dicts with text/size/color/bold/align/spacing/after."""
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.word_wrap = True
    tf.auto_size = MSO_AUTO_SIZE.NONE
    tf.margin_left = Inches(margins[0])
    tf.margin_right = Inches(margins[1])
    tf.margin_top = Inches(margins[2])
    tf.margin_bottom = Inches(margins[3])
    tf.vertical_anchor = anchor

    for i, item in enumerate(paragraphs):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = item.get("align", PP_ALIGN.LEFT)
        p.line_spacing = item.get("spacing", 1.06)
        p.space_before = Pt(item.get("before", 0))
        p.space_after = Pt(item.get("after", 0))
        if item.get("bullet"):
            b = p.add_run()
            b.text = item["bullet"]
            _set_run(
                b,
                item.get("size", 11),
                item.get("bullet_color", item.get("color", INK)),
                True,
                item.get("font", CN),
            )
        run = p.add_run()
        run.text = item["text"]
        _set_run(
            run,
            item.get("size", 11),
            item.get("color", INK),
            item.get("bold", False),
            item.get("font", CN),
            item.get("italic", False),
        )
    return box


def add_rich(
    slide,
    x,
    y,
    w,
    h,
    parts,
    size=10,
    align=PP_ALIGN.LEFT,
    anchor=MSO_ANCHOR.TOP,
    line_spacing=1.05,
):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.word_wrap = True
    tf.auto_size = MSO_AUTO_SIZE.NONE
    tf.margin_left = 0
    tf.margin_right = 0
    tf.margin_top = 0
    tf.margin_bottom = 0
    tf.vertical_anchor = anchor
    p = tf.paragraphs[0]
    p.alignment = align
    p.line_spacing = line_spacing
    for text, color, bold, *rest in parts:
        run = p.add_run()
        run.text = text
        _set_run(run, rest[0] if rest else size, color, bold)
    return box


def rect(slide, x, y, w, h, fill=CARD, line=None, radius=0.06, line_w=0.75):
    shape = slide.shapes.add_shape(
        MSO_SHAPE.ROUNDED_RECTANGLE, Inches(x), Inches(y), Inches(w), Inches(h)
    )
    try:
        shape.adjustments[0] = radius
    except Exception:
        pass
    if fill:
        shape.fill.solid()
        shape.fill.fore_color.rgb = rgb(fill)
    else:
        shape.fill.background()
    if line:
        shape.line.color.rgb = rgb(line)
        shape.line.width = Pt(line_w)
    else:
        shape.line.fill.background()
    shape.shadow.inherit = False
    return shape


def oval(slide, x, y, w, h, fill=None, line=None, line_w=1.0):
    shape = slide.shapes.add_shape(
        MSO_SHAPE.OVAL, Inches(x), Inches(y), Inches(w), Inches(h)
    )
    if fill:
        shape.fill.solid()
        shape.fill.fore_color.rgb = rgb(fill)
    else:
        shape.fill.background()
    if line:
        shape.line.color.rgb = rgb(line)
        shape.line.width = Pt(line_w)
    else:
        shape.line.fill.background()
    shape.shadow.inherit = False
    return shape


def line(slide, x1, y1, x2, y2, color=LINE, width=1.0, dash=None):
    conn = slide.shapes.add_connector(
        MSO_CONNECTOR.STRAIGHT, Inches(x1), Inches(y1), Inches(x2), Inches(y2)
    )
    conn.line.color.rgb = rgb(color)
    conn.line.width = Pt(width)
    if dash:
        conn.line.dash_style = dash
    return conn


def arrow(slide, x, y, w, h, color=TEAL):
    shape = slide.shapes.add_shape(
        MSO_SHAPE.RIGHT_ARROW, Inches(x), Inches(y), Inches(w), Inches(h)
    )
    shape.fill.solid()
    shape.fill.fore_color.rgb = rgb(color)
    shape.line.fill.background()
    shape.shadow.inherit = False
    return shape


def pill(slide, x, y, w, h, text, fill, color=WHITE, size=9, line=None, bold=True):
    rect(slide, x, y, w, h, fill, line or fill, radius=0.5, line_w=0.75)
    add_text(
        slide,
        x + 0.08,
        y,
        w - 0.16,
        h,
        text,
        size,
        color,
        bold,
        align=PP_ALIGN.CENTER,
        anchor=MSO_ANCHOR.MIDDLE,
    )


def new_slide(prs, bg=BG):
    s = prs.slides.add_slide(prs.slide_layouts[6])
    s.background.fill.solid()
    s.background.fill.fore_color.rgb = rgb(bg)
    return s


def header(slide, kicker, title, subtitle, dark=False):
    k_color = TEAL_BRIGHT if dark else TEAL
    t_color = WHITE if dark else INK
    s_color = "AFC4C1" if dark else MUTED
    add_text(slide, M, 0.34, 10.5, 0.25, kicker, 10, k_color, True)
    add_text(slide, M, 0.61, CW, 0.62, title, 27, t_color, True, line_spacing=1.0)
    rect(slide, M, 1.27, 0.45, 0.05, AMBER, None, radius=0.5)
    add_text(slide, M, 1.38, CW, 0.34, subtitle, 12, s_color, False, line_spacing=1.0)


def footer(slide, page, dark=False):
    c = "7E9592" if not dark else "6F8683"
    line(slide, M, 7.03, SW - M, 7.03, "D9E5E1" if not dark else "1E444A", 0.6)
    add_text(slide, M, 7.1, 5.5, 0.2, "NutriBot · 心屿科技", 8, c)
    label = f"{page:02d}" if isinstance(page, int) else str(page)
    add_text(slide, SW - M - 1.0, 7.1, 1.0, 0.2, label, 8, c, align=PP_ALIGN.RIGHT)


def notes(slide, text):
    slide.notes_slide.notes_text_frame.text = text


def set_cell(cell, text, size=10, color=INK, bold=False, fill=CARD, align=PP_ALIGN.LEFT):
    cell.fill.solid()
    cell.fill.fore_color.rgb = rgb(fill)
    cell.margin_left = Inches(0.09)
    cell.margin_right = Inches(0.06)
    cell.margin_top = Inches(0.02)
    cell.margin_bottom = Inches(0.02)
    cell.vertical_anchor = MSO_ANCHOR.MIDDLE
    tf = cell.text_frame
    tf.word_wrap = True
    p = tf.paragraphs[0]
    p.alignment = align
    p.space_before = Pt(0)
    p.space_after = Pt(0)
    run = p.add_run()
    run.text = str(text)
    _set_run(run, size, color, bold)


def cell_border(cell, color, width_pt=0.75):
    tc_pr = cell._tc.get_or_add_tcPr()
    for tag in ("a:lnL", "a:lnR", "a:lnT", "a:lnB"):
        ln = tc_pr.find(qn(tag))
        if ln is None:
            ln = etree.SubElement(tc_pr, qn(tag))
        ln.set("w", str(int(width_pt * 12700)))
        ln.set("cap", "flat")
        ln.set("cmpd", "sng")
        ln.set("algn", "ctr")
        for child in list(ln):
            ln.remove(child)
        fill = etree.SubElement(ln, qn("a:solidFill"))
        clr = etree.SubElement(fill, qn("a:srgbClr"))
        clr.set("val", color)


def add_table(
    slide,
    x,
    y,
    w,
    col_widths,
    rows,
    row_h=0.55,
    header_fill=TEAL,
    header_color=WHITE,
    font_size=10,
    header_size=10,
):
    table_shape = slide.shapes.add_table(
        len(rows), len(col_widths), Inches(x), Inches(y), Inches(w), Inches(row_h * len(rows))
    )
    table = table_shape.table
    table.first_row = True
    table.horz_banding = False
    for i, cw in enumerate(col_widths):
        table.columns[i].width = Inches(cw)
    for r, row in enumerate(rows):
        table.rows[r].height = Inches(row_h)
        for c, value in enumerate(row):
            is_head = r == 0
            fill = header_fill if is_head else (CARD if r % 2 == 1 else "F3F8F6")
            set_cell(
                table.cell(r, c),
                value,
                header_size if is_head else font_size,
                header_color if is_head else INK,
                is_head,
                fill,
            )
            cell_border(table.cell(r, c), fill)
    return table


# ---------------------------------------------------------------------------
# Slides
# ---------------------------------------------------------------------------


def slide_cover(prs):
    s = new_slide(prs, DARK)

    # Plate / orbit motif
    oval(s, 7.85, 1.0, 4.9, 4.9, DARK_2)
    oval(s, 8.2, 1.35, 4.2, 4.2, DARK_3)
    oval(s, 8.62, 1.77, 3.36, 3.36, "F7F9F8")
    oval(s, 8.82, 1.97, 2.96, 2.96, None, "DCE7E4", 1.0)
    oval(s, 9.12, 2.27, 2.36, 2.36, None, "CBE3DC", 0.9)

    oval(s, 9.45, 2.5, 0.95, 0.95, "8FBF9F")
    oval(s, 10.42, 2.62, 0.86, 0.86, "E8B36C")
    oval(s, 9.95, 3.28, 0.82, 0.82, "D98C7A")
    oval(s, 9.68, 2.7, 0.22, 0.22, "F7F9F8")
    oval(s, 10.58, 3.12, 0.18, 0.18, "F7F9F8")
    oval(s, 10.16, 3.55, 0.16, 0.16, "F7F9F8")

    pill(s, 7.55, 1.08, 1.52, 0.32, "约 320 kcal", DARK_3, WHITE, 8.5, line="2C5A5F")
    pill(s, 11.05, 1.55, 1.5, 0.32, "蛋白质 28g", DARK_3, WHITE, 8.5, line="2C5A5F")
    pill(s, 11.28, 4.35, 1.3, 0.32, "钠 420mg", DARK_3, WHITE, 8.5, line="2C5A5F")

    # Peeking robot head
    rect(s, 11.05, 5.5, 1.05, 0.86, DARK_3, TEAL_BRIGHT, radius=0.35, line_w=1.2)
    oval(s, 11.25, 5.7, 0.13, 0.18, WHITE)
    oval(s, 11.6, 5.7, 0.13, 0.18, WHITE)
    line(s, 11.58, 5.5, 11.58, 5.22, TEAL_BRIGHT, 1.2)
    oval(s, 11.54, 5.14, 0.08, 0.08, AMBER)

    # Copy
    pill(s, 0.78, 0.78, 6.55, 0.36, "2026 高通具身智能与机器人大赛 · 具身智能与机器人赛道", TEAL, WHITE, 9.5)
    add_text(s, 0.78, 1.52, 7.1, 0.95, "NutriBot 营养管家", 42, WHITE, True, line_spacing=1.0)
    add_text(s, 0.82, 2.5, 7.0, 0.55, "餐桌旁的端侧 AI 健康伙伴", 22, TEAL_BRIGHT, True)
    add_text(s, 0.82, 3.18, 6.4, 0.4, "看得见一桌菜，记得住一家人。", 16, "C9D8D5")

    chips = [("本地化部署", 0.82), ("语音全链路", 2.55), ("LLM 看菜对话", 4.28)]
    for text, x in chips:
        pill(s, x, 4.05, 1.55, 0.38, text, DARK_3, "DDECE8", 9.5, line="2C5A5F")

    add_text(s, 0.82, 6.35, 6.0, 0.25, "心屿科技 · NutriBot 项目组   |   2026.11", 9.5, "8FA7A3")

    notes(
        s,
        "各位评委好，我们是心屿科技的 NutriBot 项目。"
        "一句话介绍：这是一台放在餐桌旁的端侧 AI 营养管家，能自己看菜、主动提醒、用语音和动作陪家人吃饭。"
        "今天重点讲三件事：为什么餐桌需要它、我们怎么用端侧 AI 做出来、以及它能走向多大的市场。",
    )


def slide_overview(prs, page):
    s = new_slide(prs)
    header(
        s,
        "00 / 一页看懂",
        "把营养师，放到餐桌旁",
        "从「人记录饮食」到「设备主动理解一餐」",
    )

    stats = [
        ("3.1 亿", "60 岁及以上人口", "老龄化加速，饮食干预是高频入口"),
        ("4 亿+", "慢病人群", "饮食是首要可干预因素"),
        ("66 道", "菜品营养库", "每份 6 项营养素，可追溯"),
        ("8B", "端侧多模态模型", "本地 NPU 运行，可全离线"),
    ]
    for i, (num, label, desc) in enumerate(stats):
        x = M + i * 3.0
        rect(s, x, 1.95, 2.75, 1.55, CARD, LINE, radius=0.07)
        add_text(s, x + 0.22, 2.1, 2.3, 0.5, num, 29, TEAL, True, font=NUM)
        add_text(s, x + 0.22, 2.62, 2.35, 0.25, label, 10, INK, True)
        add_text(s, x + 0.22, 2.95, 2.35, 0.45, desc, 9.2, MUTED, line_spacing=1.05)

    rect(s, M, 3.78, CW, 0.98, DARK, None, radius=0.06)
    add_text(
        s,
        M + 0.3,
        3.92,
        CW - 0.6,
        0.38,
        "NutriBot = 多模态感知 × 家庭营养档案 × 主动语音交互 × 具身表达",
        16,
        WHITE,
        True,
        align=PP_ALIGN.CENTER,
    )
    add_text(
        s,
        M + 0.3,
        4.36,
        CW - 0.6,
        0.25,
        "不是另一个健康 App，而是餐桌旁的一个「人」。",
        10.5,
        "AFC4C1",
        align=PP_ALIGN.CENTER,
    )

    pillars = [
        ("01", "看见", "摄像头周期理解桌面：有哪些菜、是否有人、处于哪一餐。"),
        ("02", "理解", "家庭成员档案、过敏忌口、慢病阈值与营养库。"),
        ("03", "表达", "主动开口提醒，用表情、夹爪和移动回应。"),
    ]
    for i, (num, title, desc) in enumerate(pillars):
        x = M + i * 4.03
        rect(s, x, 5.08, 3.83, 1.42, CARD, LINE, radius=0.07)
        oval(s, x + 0.2, 5.26, 0.43, 0.43, MINT)
        add_text(s, x + 0.2, 5.31, 0.43, 0.25, num, 9, TEAL, True, align=PP_ALIGN.CENTER, font=NUM)
        add_text(s, x + 0.76, 5.2, 2.8, 0.32, title, 15, INK, True)
        add_text(s, x + 0.76, 5.58, 2.85, 0.7, desc, 9.5, MUTED, line_spacing=1.08)

    footer(s, page)
    notes(
        s,
        "这一页是全场摘要。四个数字分别对应需求、市场、知识库和模型能力。"
        "核心公式是：多模态感知、家庭营养档案、主动语音交互、具身表达，四件事合在一起才是 NutriBot。"
        "我们不是做一个记录工具，而是让设备主动理解一顿饭。",
    )


def slide_pain(prs, page):
    s = new_slide(prs)
    header(
        s,
        "01 / 痛点",
        "餐桌，是健康管理最难触达的最后一米",
        "知识不缺，缺的是「在场」的感知与提醒",
    )

    pains = [
        ("01", "看不见", "一顿饭的油盐糖，靠感觉，不靠数据。高盐、高糖、高脂往往在吃完后才被想起。"),
        ("02", "记不住", "手动记录需要每餐拍照、称重、输入；多数人坚持不过三天，数据很快断档。"),
        ("03", "管不了", "通用建议不区分家庭成员；老人控盐、孩子成长、慢病忌口，需要不同的提醒。"),
    ]
    for i, (num, title, desc) in enumerate(pains):
        y = 1.95 + i * 1.34
        oval(s, M, y + 0.08, 0.52, 0.52, TEAL)
        add_text(s, M, y + 0.15, 0.52, 0.26, num, 10, WHITE, True, align=PP_ALIGN.CENTER, font=NUM)
        add_text(s, M + 0.72, y, 6.2, 0.38, title, 18, INK, True)
        add_text(s, M + 0.72, y + 0.42, 6.1, 0.78, desc, 10.5, MUTED, line_spacing=1.12)

    rect(s, 8.15, 1.95, 4.45, 3.28, AMBER_SOFT, AMBER, radius=0.07)
    add_text(s, 8.42, 2.16, 3.9, 0.25, "一个真实的餐桌瞬间", 9.5, AMBER, True)
    add_text(
        s,
        8.42,
        2.55,
        3.9,
        1.05,
        "“妈，今天这汤咸不咸？”\n“不咸，就放了一点盐。”",
        15,
        INK,
        True,
        line_spacing=1.2,
    )
    line(s, 8.42, 3.62, 12.3, 3.62, "E7C9A7", 0.8)
    add_text(
        s,
        8.42,
        3.78,
        3.9,
        0.9,
        "但没有人知道，这碗汤的钠已经接近全天上限。",
        11,
        INK_SOFT,
        True,
        line_spacing=1.1,
    )
    add_text(s, 8.42, 4.62, 3.9, 0.4, "健康管理缺的不是知识，是「当下」的反馈。", 9.5, MUTED)

    stats = [
        ("3.1 亿", "60+ 人口，占比 22.0%"),
        ("4 亿+", "慢病人群，饮食可干预"),
        ("50.7%", "成人超重 / 肥胖率"),
    ]
    for i, (num, label) in enumerate(stats):
        x = M + i * 4.03
        rect(s, x, 5.55, 3.83, 0.76, CARD, LINE, radius=0.06)
        add_text(s, x + 0.22, 5.66, 1.35, 0.32, num, 17, TEAL, True, font=NUM)
        add_text(s, x + 1.55, 5.7, 2.1, 0.35, label, 9.5, MUTED)

    add_text(
        s,
        M,
        6.5,
        CW,
        0.25,
        "数据来源：国家统计局《2024 年国民经济和社会发展统计公报》；《中国居民营养与慢性病状况报告（2020 年）》。",
        7.5,
        "8A9C99",
    )

    footer(s, page)
    notes(
        s,
        "先看痛点。餐桌上的健康管理有三个断点：看不见、记不住、管不了。"
        "右边这个场景很典型：家人凭感觉判断咸淡，但钠摄入已经接近全天上限。"
        "现有工具要么靠手动记录，要么给通用建议，都不能在当下、针对具体的人给出反馈。",
    )


def slide_gap(prs, page):
    s = new_slide(prs)
    header(
        s,
        "02 / 现有方案",
        "为什么现有产品，解决不了这一餐？",
        "四个看似相关的品类，都缺了同一块拼图",
    )

    cards = [
        ("App", "手机营养 App", "食物库、热量查询、手动记录", "依赖手动输入，事后记录，看不见餐桌。"),
        ("箱", "智能冰箱", "库存、采购、保鲜管理", "知道有什么食材，不知道这一餐吃了多少"),
        ("音", "通用语音助手", "语音问答、内容生态", "没有家庭档案、营养规则和视觉感知。"),
        ("师", "人类营养师", "专业评估、个性化方案", "昂贵、低频，无法陪伴每一餐。"),
    ]
    for i, (icon, title, good, bad) in enumerate(cards):
        x = M + i * 3.0
        rect(s, x, 1.95, 2.75, 3.35, CARD, LINE, radius=0.07)
        oval(s, x + 0.22, 2.17, 0.58, 0.58, MINT)
        add_text(s, x + 0.22, 2.27, 0.58, 0.3, icon, 10, TEAL, True, align=PP_ALIGN.CENTER)
        add_text(s, x + 0.94, 2.25, 1.7, 0.45, title, 13.5, INK, True)
        add_text(s, x + 0.22, 2.98, 2.3, 0.2, "解决了", 8.5, TEAL, True)
        add_text(s, x + 0.22, 3.2, 2.3, 0.65, good, 9.3, MUTED, line_spacing=1.08)
        add_text(s, x + 0.22, 3.98, 2.3, 0.2, "缺什么", 8.5, CORAL, True)
        add_text(s, x + 0.22, 4.2, 2.3, 0.85, bad, 9.3, INK_SOFT, line_spacing=1.08)

    rect(s, M, 5.55, CW, 1.0, DARK, None, radius=0.06)
    add_text(
        s,
        M + 0.3,
        5.68,
        CW - 0.6,
        0.38,
        "市场缺的不是营养知识，而是一个「在场」的感知与提醒入口。",
        16,
        WHITE,
        True,
        align=PP_ALIGN.CENTER,
    )
    add_text(
        s,
        M + 0.3,
        6.12,
        CW - 0.6,
        0.25,
        "NutriBot 的位置：主动 × 垂直 × 具身 × 家庭",
        10,
        TEAL_BRIGHT,
        True,
        align=PP_ALIGN.CENTER,
    )

    footer(s, page)
    notes(
        s,
        "现有方案各有价值，但都缺同一块拼图：对「这一餐、这家人」的实时感知。"
        "手机 App 靠手动，智能冰箱只知道库存，通用语音助手没有家庭档案，营养师专业但低频昂贵。"
        "所以我们的定位是主动、垂直、具身、面向家庭。",
    )


def slide_definition(prs, page):
    s = new_slide(prs)
    header(
        s,
        "03 / 产品定义",
        "NutriBot：餐桌旁的 AI 营养管家",
        "看见一桌菜，理解一家人，陪伴每一餐",
    )

    pillars = [
        ("看", "看见一桌菜", "多模态视觉周期感知；无需唤醒、无需手动记录。"),
        ("懂", "理解一家人", "过敏、忌口、慢病、年龄、DRI，逐人计算。"),
        ("陪", "陪伴每一餐", "主动开口、自然对话、表情与动作；该沉默时会沉默。"),
    ]
    for i, (badge, title, desc) in enumerate(pillars):
        y = 1.95 + i * 1.55
        oval(s, M, y + 0.12, 0.66, 0.66, MINT, MINT_2, 1.0)
        add_text(s, M, y + 0.26, 0.66, 0.32, badge, 15, TEAL, True, align=PP_ALIGN.CENTER)
        add_text(s, M + 0.88, y + 0.04, 5.4, 0.38, title, 18, INK, True)
        add_text(s, M + 0.88, y + 0.48, 5.4, 0.7, desc, 10.8, MUTED, line_spacing=1.12)

    rect(s, 8.1, 1.95, 4.5, 4.55, CARD, LINE, radius=0.07)
    add_text(s, 8.38, 2.15, 3.9, 0.25, "为谁而做", 9.5, TEAL, True)
    personas = [
        ("独居老人", "需要被看见的每一餐"),
        ("慢病家庭", "控盐控糖，不靠意志力"),
        ("成长中的孩子", "在餐桌上建立食育"),
    ]
    for i, (name, desc) in enumerate(personas):
        y = 2.58 + i * 0.92
        oval(s, 8.38, y + 0.05, 0.34, 0.34, AMBER_SOFT)
        add_text(s, 8.38, y + 0.12, 0.34, 0.2, str(i + 1), 8, AMBER, True, align=PP_ALIGN.CENTER, font=NUM)
        add_text(s, 8.86, y, 3.3, 0.3, name, 12.5, INK, True)
        add_text(s, 8.86, y + 0.3, 3.3, 0.28, desc, 9.5, MUTED)
        if i < 2:
            line(s, 8.38, y + 0.76, 12.1, y + 0.76, "E8F0ED", 0.7)

    line(s, 8.38, 5.48, 12.1, 5.48, "E8F0ED", 0.7)
    add_text(
        s,
        8.38,
        5.7,
        3.9,
        0.55,
        "不是另一个健康 App，\n而是一个「在场」的生活伙伴。",
        11,
        INK_SOFT,
        True,
        line_spacing=1.2,
    )

    footer(s, page)
    notes(
        s,
        "NutriBot 的产品定义很简单：看见、理解、陪伴。"
        "看见是视觉感知，理解是家庭档案和营养规则，陪伴是语音、表情和动作。"
        "目标人群先聚焦三类：独居老人、慢病家庭、成长中的孩子。",
    )


def slide_hardware(prs, page):
    s = new_slide(prs)
    header(
        s,
        "04 / 产品形态",
        "一台能看、能说、能动手的桌面机器人",
        "SoC 负责智能，MCU 负责实时；工程样机已跑通",
    )

    # Table surface
    rect(s, 0.95, 6.0, 5.8, 0.14, "D9C7B6", None, radius=0.2)
    oval(s, 5.55, 5.78, 0.9, 0.28, "F1E9E0", "D9C7B6", 0.8)

    # Robot
    rect(s, 3.35, 5.18, 1.7, 0.52, DARK_3, None, radius=0.2)
    oval(s, 3.5, 5.58, 0.42, 0.42, "2A4A50")
    oval(s, 4.5, 5.58, 0.42, 0.42, "2A4A50")
    rect(s, 3.67, 4.05, 1.05, 1.18, CARD, TEAL, radius=0.2, line_w=1.2)
    oval(s, 3.4, 2.95, 1.6, 1.6, INK, TEAL_BRIGHT, 1.4)
    oval(s, 3.62, 3.17, 1.16, 1.16, DARK, None)
    oval(s, 3.9, 3.55, 0.13, 0.18, WHITE)
    oval(s, 4.36, 3.55, 0.13, 0.18, WHITE)
    line(s, 4.02, 3.95, 4.4, 3.95, TEAL_BRIGHT, 1.4)
    line(s, 4.2, 2.95, 4.2, 2.68, TEAL_BRIGHT, 1.2)
    oval(s, 4.16, 2.6, 0.08, 0.08, AMBER)

    # Arm
    line(s, 4.7, 4.35, 5.45, 4.05, INK_SOFT, 1.8)
    line(s, 5.45, 4.05, 5.7, 4.25, INK_SOFT, 1.8)
    line(s, 5.7, 4.25, 5.88, 4.08, INK_SOFT, 1.6)
    line(s, 5.7, 4.25, 5.9, 4.38, INK_SOFT, 1.6)

    # Callouts
    callouts = [
        ("广角视觉", 0.88, 2.45, 3.4, 3.35),
        ("2 麦阵列 + 扬声器", 0.88, 3.75, 3.67, 4.55),
        ("OLED 表情屏", 5.55, 2.55, 4.7, 3.45),
        ("6 舵机夹爪", 5.55, 3.88, 5.78, 4.2),
        ("双轮底盘", 1.15, 5.2, 3.55, 5.42),
        ("USB-C 供电", 5.45, 5.2, 4.45, 5.4),
    ]
    for text, x, y, tx, ty in callouts:
        pill(s, x, y, 1.45, 0.32, text, MINT, TEAL, 8.2, line=MINT_2)
        line(s, x + 1.45 if x < 4 else x, y + 0.16, tx, ty, "B7CFC8", 0.7, MSO_LINE.DASH)

    # SoC / MCU cards
    rect(s, 7.55, 1.95, 4.95, 1.3, MINT, MINT_2, radius=0.07)
    add_text(s, 7.8, 2.1, 4.4, 0.3, "SoC · 高通 QCS8550", 14.5, TEAL, True)
    add_text(s, 7.8, 2.44, 4.4, 0.3, "Linux + NPU", 9.5, MUTED, True)
    add_text(s, 7.8, 2.72, 4.4, 0.35, "视觉 · 语音 · LLM · 规则 · 编排", 10, INK_SOFT)

    pill(s, 9.35, 3.42, 1.35, 0.28, "Wi-Fi / HTTP", DARK_3, WHITE, 7.8)

    rect(s, 7.55, 3.85, 4.95, 1.3, AMBER_SOFT, "EBC9A6", radius=0.07)
    add_text(s, 7.8, 4.0, 4.4, 0.3, "MCU · ESP32-S3", 14.5, AMBER, True)
    add_text(s, 7.8, 4.34, 4.4, 0.3, "实时控制", 9.5, MUTED, True)
    add_text(s, 7.8, 4.62, 4.4, 0.35, "双轮底盘 · 6 舵机 · OLED 表情 · 安全停", 9.6, INK_SOFT)

    add_text(
        s,
        7.8,
        5.42,
        4.45,
        0.85,
        "视觉：广角摄像头（目标 RGB-D 120°）\n语音：2 麦阵列 + 3W 扬声器\n供电：USB-C · 无电池",
        8.8,
        MUTED,
        line_spacing=1.25,
    )
    add_text(s, 7.8, 6.3, 4.5, 0.28, "目标：BOM < 800 元 · 售价 1500–2500 元", 10, TEAL, True)

    footer(s, page)
    notes(
        s,
        "硬件是双芯片架构：高通 QCS8550 作为 SoC，跑 Linux 和 NPU，负责视觉、语音、大模型、规则和编排；"
        "ESP32-S3 作为 MCU，负责底盘、舵机、表情屏和安全停这些实时控制。"
        "AI 决策在 SoC，运动闭环在 MCU，通过 Wi-Fi/HTTP 通信。这样既保证智能，又保证实时和安全。"
        "当前是工程样机，目标形态是消费级一体化设计，BOM 控制在 800 元以内。",
    )


def slide_experience(prs, page):
    s = new_slide(prs)
    header(
        s,
        "05 / 用户体验",
        "一顿饭的六个瞬间",
        "从入座到撤桌，设备主动完成感知、提醒与记录",
    )

    moments = [
        ("18:00", "入座问候", "报饭点，提醒上菜"),
        ("18:01", "上菜识别", "多菜同框，逐道确认"),
        ("18:01", "营养播报", "菜名 + 热量 + 风险"),
        ("18:02", "风险提醒", "按家人档案点名"),
        ("18:10", "用餐问答", "结合当前餐桌事实"),
        ("18:30", "餐后小结", "菜品、热量、用时"),
    ]
    dots = []
    for i, (time, title, desc) in enumerate(moments):
        x = M + i * 1.94
        rect(s, x, 1.95, 1.78, 1.62, CARD, LINE, radius=0.07)
        add_text(s, x + 0.15, 2.08, 1.45, 0.2, time, 8.5, AMBER, True, font=NUM)
        add_text(s, x + 0.15, 2.32, 1.5, 0.3, title, 11.5, INK, True)
        add_text(s, x + 0.15, 2.7, 1.48, 0.7, desc, 8.6, MUTED, line_spacing=1.08)
        dots.append(x + 0.89)

    line(s, 0.95, 4.0, 12.1, 4.0, "C9DDD7", 2.0)
    triggers = ["饭点 / 入座", "检测到菜", "营养库", "冲突计算", "语音提问", "撤桌 / 安静"]
    for i, x in enumerate(dots):
        oval(s, x - 0.075, 3.925, 0.15, 0.15, TEAL, BG, 1.5)
        add_text(s, x - 0.6, 4.14, 1.2, 0.22, triggers[i], 7.8, MUTED, align=PP_ALIGN.CENTER)

    rect(s, M, 4.72, CW, 1.62, CARD, LINE, radius=0.07)
    add_text(s, M + 0.28, 4.88, 2.0, 0.25, "它会这样说", 9.5, TEAL, True)
    add_text(
        s,
        M + 0.28,
        5.16,
        CW - 0.56,
        1.0,
        "“今天有清蒸鲈鱼、番茄炒蛋和紫菜蛋花汤。”\n"
        "“王奶奶在控盐，紫菜蛋花汤的钠偏高，建议少喝一点。”\n"
        "“这顿一共 3 道菜，约 620 千卡。”",
        13,
        INK,
        line_spacing=1.35,
    )
    add_text(
        s,
        M,
        6.55,
        CW,
        0.25,
        "主动播报链路实测 3.3s（3B 轻量档）；8B 高精度档单帧看菜约 16s，按需启用。语音首响另有填充语优化：约 0.7s 开口。",
        8.2,
        "8A9C99",
    )

    footer(s, page)
    notes(
        s,
        "这是用户视角的一顿饭。入座时问候，上菜后识别，随后报菜名和热量，有风险按家庭成员点名提醒。"
        "吃饭过程中可以随时提问，撤桌后自动生成小结。"
        "整个过程中用户不需要掏手机、不需要记录，设备主动完成。",
    )


def slide_features(prs, page):
    s = new_slide(prs)
    header(
        s,
        "06 / 功能特色",
        "六项能力，组成一个完整的用餐闭环",
        "感知 → 理解 → 提醒 → 对话 → 表达 → 记录",
    )

    features = [
        ("01", "主动看菜", "周期感知桌面，无需唤醒；连续帧稳定后才播报，抑制空画面幻觉。", "无需手动记录"),
        ("02", "营养量化", "每份热量、蛋白质、脂肪、碳水、钠、糖；播报取整到 10。", "6 项营养素"),
        ("03", "家庭档案", "过敏、忌口、慢病、年龄语气、DRI；逐人计算冲突。", "一人一策"),
        ("04", "自然对话", "唤醒词 + 连续对话 + 流式播报；用户没开口时不抢话。", "半双工"),
        ("05", "具身表达", "表情、夹爪、底盘动作；说到做到，动作有安全边界。", "语音 + 动作"),
        ("06", "本地化部署", "模型、语音、数据全部在设备端；断网可用，数据不出户。", "端侧 AI"),
    ]
    for i, (num, title, desc, tag) in enumerate(features):
        col, row = i % 3, i // 3
        x = M + col * 4.04
        y = 1.95 + row * 2.2
        rect(s, x, y, 3.8, 2.0, CARD, LINE, radius=0.07)
        oval(s, x + 0.22, y + 0.2, 0.42, 0.42, MINT)
        add_text(s, x + 0.22, y + 0.25, 0.42, 0.24, num, 8.5, TEAL, True, align=PP_ALIGN.CENTER, font=NUM)
        add_text(s, x + 0.78, y + 0.2, 2.7, 0.35, title, 14.5, INK, True)
        add_text(s, x + 0.22, y + 0.75, 3.35, 0.85, desc, 9.6, MUTED, line_spacing=1.12)
        pill(s, x + 0.22, y + 1.62, 1.35, 0.25, tag, MINT, TEAL, 7.8, line=MINT_2)

    add_text(
        s,
        M,
        6.5,
        CW,
        0.25,
        "从「看见」到「说出」，再到「做出」，形成主动健康管理的闭环。",
        10,
        MUTED,
        align=PP_ALIGN.CENTER,
    )

    footer(s, page)
    notes(
        s,
        "六项能力可以分成三组：感知组是主动看菜和营养量化；理解组是家庭档案和自然对话；"
        "表达组是具身交互和本地化部署。"
        "特别强调第六项：所有模型、语音和数据都在设备端，断网也能用，隐私不出户。",
    )


def slide_architecture(prs, page):
    s = new_slide(prs)
    header(
        s,
        "07 / 技术架构",
        "软硬协同：SoC 做智能，MCU 做实时",
        "感知、认知、语音与编排在 SoC；运动、舵机、表情与安全停在 MCU",
    )

    layers = [
        ("编排层", "ResidentSession", "主动调度", "半双工", "流式播报", MINT),
        ("认知层", "Agent Engine", "Qwen3-VL-8B", "工具调用", "营养规则", MINT_2),
        ("感知层", "摄像头", "VAD", "SenseVoice", "场景 / 饭点", CARD),
        ("表达层", "MeloTTS", "分句流式", "表情", "夹爪", CARD),
        ("数据层", "成员档案", "66 道营养库", "餐账", "会话历史", "EEF3F1"),
    ]
    for i, (label, c1, c2, c3, c4, fill) in enumerate(layers):
        y = 1.95 + i * 0.9
        rect(s, M, y, 1.05, 0.72, fill, LINE, radius=0.06)
        add_text(s, M, y + 0.17, 1.05, 0.3, label, 10.5, INK, True, align=PP_ALIGN.CENTER)
        rect(s, 1.9, y, 8.0, 0.72, fill, LINE, radius=0.06)
        for j, chip in enumerate((c1, c2, c3, c4)):
            cx = 2.08 + j * 1.93
            pill(s, cx, y + 0.18, 1.75, 0.35, chip, WHITE, INK_SOFT, 8.3, line=LINE, bold=False)

    # SoC / MCU side panel
    rect(s, 10.15, 1.95, 2.45, 1.55, MINT, MINT_2, radius=0.07)
    add_text(s, 10.32, 2.1, 2.1, 0.25, "SoC · QCS8550", 10.5, TEAL, True)
    add_text(s, 10.32, 2.4, 2.1, 0.9, "Linux + NPU\n视觉 · 语音 · LLM\n规则 · 编排", 8.8, INK_SOFT, line_spacing=1.15)

    pill(s, 10.65, 3.63, 1.45, 0.26, "Wi-Fi / HTTP", DARK_3, WHITE, 7.2)

    rect(s, 10.15, 4.02, 2.45, 1.55, AMBER_SOFT, "EBC9A6", radius=0.07)
    add_text(s, 10.32, 4.17, 2.1, 0.25, "MCU · ESP32-S3", 10.5, AMBER, True)
    add_text(s, 10.32, 4.47, 2.1, 0.9, "实时控制\n双轮 · 6 舵机\nOLED · 安全停", 8.8, INK_SOFT, line_spacing=1.15)

    add_text(
        s,
        10.15,
        5.8,
        2.45,
        0.55,
        "AI 决策在 SoC，\n运动闭环在 MCU。",
        8.8,
        MUTED,
        True,
        line_spacing=1.15,
    )

    add_text(
        s,
        M,
        6.42,
        CW,
        0.25,
        "SoC 侧负责「看懂、想清、说好」；MCU 侧负责「动得准、停得住」。",
        9,
        "8A9C99",
    )

    footer(s, page)
    notes(
        s,
        "架构上最值得讲的是软硬协同。左边是软件栈：编排、认知、感知、表达、数据五层。"
        "右边是硬件：SoC 跑 Linux 和 NPU，承载视觉、语音、大模型与规则；MCU 跑实时控制，负责底盘、舵机、表情和安全停。"
        "两者通过 Wi-Fi/HTTP 通信，AI 决策在 SoC，运动闭环在 MCU，职责分离，失效安全。",
    )


def slide_model_stack(prs, page):
    s = new_slide(prs)
    header(
        s,
        "08 / 端侧 AI 栈",
        "8B 多模态模型，跑在 NPU 上",
        "语音、视觉、语言、动作四类模型统一编排，全部本地运行",
    )

    rows = [
        ["能力", "模型 / 组件", "运行方式", "实测表现"],
        ["多模态理解", "Qwen3-VL-8B-Instruct", "QNN2.48 · HTP · 4K ctx", "多菜 5/5；单帧 8 道"],
        ["语音识别", "SenseVoiceSmall", "AidVoice · 本地", "中文短句稳定"],
        ["语音合成", "MeloTTS-Chinese", "AidVoice · 本地", "流式分句播报"],
        ["语音活动检测", "WebRTC VAD", "本地 C++", "半双工防抢话"],
        ["动作控制", "ESP32-S3 固件", "局域网 HTTP", "表情 / 夹爪 / 底盘"],
        ["主动编排", "ResidentSession", "C++17 · 100ms tick", "3.3s（3B 轻量档）"],
    ]
    add_table(s, M, 1.95, CW, [2.0, 3.5, 3.2, 3.19], rows, row_h=0.56, font_size=9.6)

    chips = [
        ("本地化部署", "模型与数据在设备端"),
        ("语音全链路", "ASR → LLM → TTS"),
        ("LLM 编排", "看菜 · 对话 · 工具"),
        ("具身执行", "表情 / 夹爪 / 底盘"),
    ]
    for i, (title, desc) in enumerate(chips):
        x = M + i * 3.0
        rect(s, x, 6.0, 2.85, 0.62, MINT, MINT_2, radius=0.06)
        add_text(s, x + 0.16, 6.08, 2.55, 0.22, title, 9.5, TEAL, True)
        add_text(s, x + 0.16, 6.32, 2.55, 0.2, desc, 8, MUTED)

    add_text(
        s,
        M,
        6.72,
        CW,
        0.2,
        "开发期支持云端 / 端侧切换；量产默认端侧，断网可用。",
        7.8,
        "8A9C99",
    )

    footer(s, page)
    notes(
        s,
        "这一页是模型栈。最核心的是 Qwen3-VL-8B，通过 QNN2.48 跑在高通 HTP 上，4K 上下文。"
        "语音识别用 SenseVoice，合成用 MeloTTS，VAD 用 WebRTC，动作控制交给 ESP32 固件。"
        "这些能力由 ResidentSession 统一编排。重点是：不是调用云端 API，而是把整条链路放在设备端。",
    )


def slide_proactive(prs, page):
    s = new_slide(prs)
    header(
        s,
        "09 / 关键技术①",
        "主动感知：该开口时才开口",
        "用状态机、稳定性门控与合并窗，把「主动」做成「不打扰」",
    )

    states = [
        ("空桌待机", "无运动 · 无菜", False),
        ("备餐中", "检测到运动", False),
        ("用餐中", "有人 + 有菜", True),
        ("餐后", "撤桌 + 安静", False),
    ]
    for i, (title, desc, active) in enumerate(states):
        x = M + i * 2.98
        fill = TEAL if active else CARD
        line_color = TEAL if active else LINE
        rect(s, x, 1.95, 2.35, 1.08, fill, line_color, radius=0.07)
        add_text(s, x + 0.18, 2.1, 2.0, 0.3, title, 14.5, WHITE if active else INK, True)
        add_text(s, x + 0.18, 2.52, 2.0, 0.25, desc, 8.6, "D6F3EE" if active else MUTED)
        if i < 3:
            arrow(s, x + 2.42, 2.42, 0.42, 0.12, TEAL_BRIGHT)
            add_text(s, x + 2.36, 2.16, 0.55, 0.2, ["运动", "有菜", "撤桌"][i], 7.5, MUTED, align=PP_ALIGN.CENTER)

    design = [
        ("连续帧稳定门控", "连续多帧一致才播报，压掉空画面的幻觉"),
        ("2.5s 冲突合并窗", "用户先开口 → 并入本轮对话，不重复播报"),
        ("15s 事实新鲜度", "过期事实不再注入，避免复述旧菜"),
        ("半双工 + 沉默判断", "用户说话时不插话；无需回应时只记日志"),
    ]
    for i, (title, desc) in enumerate(design):
        col, row = i % 2, i // 2
        x = M + col * 5.93
        y = 3.55 + row * 1.22
        rect(s, x, y, 5.8, 1.08, CARD, LINE, radius=0.07)
        add_text(s, x + 0.22, y + 0.15, 5.3, 0.3, title, 12.5, INK, True)
        add_text(s, x + 0.22, y + 0.52, 5.3, 0.4, desc, 9.3, MUTED)

    rect(s, M, 6.08, CW, 0.72, DARK, None, radius=0.06)
    add_text(s, M + 0.25, 6.2, 6.5, 0.3, "放菜 → 主动开口：22s → 3.3s（3B 轻量档实测）", 11, WHITE, True)
    add_text(s, M + 0.25, 6.5, 6.8, 0.2, "8B 单帧看菜约 16s，按需启用；误触发目标 <10%", 7.8, "AFC4C1")
    add_text(s, 8.4, 6.2, 4.0, 0.3, "先稳定，再开口；先理解，再提醒。", 10, TEAL_BRIGHT, True, align=PP_ALIGN.RIGHT)

    footer(s, page)
    notes(
        s,
        "主动感知的关键不是「一直说」，而是「该说的时候说」。"
        "左边是四个状态：空桌待机、备餐中、用餐中、餐后。只有连续多帧稳定、有人有菜，才进入播报。"
        "右边四个设计防止打扰：连续帧门控、冲突合并窗、事实新鲜度、半双工与沉默判断。"
        "时延从最早的 22 秒压到 3.3 秒（3B 轻量档）；8B 高精度档单帧看菜约 16 秒，按需启用。",
    )


def slide_nutrition(prs, page):
    s = new_slide(prs)
    header(
        s,
        "10 / 关键技术②",
        "营养事实由代码算，话由模型说",
        "66 道菜 × 6 项营养素 × 家庭档案 × 慢病阈值",
    )

    steps = [
        ("识别与归一", "VL 菜名 → 别名 / 最长匹配 → 营养库规范名"),
        ("营养查询", "每份 6 项：热量、蛋白、脂肪、碳水、钠、糖"),
        ("规则计算", "过敏 / 忌口 + 高钠 / 高糖 / 高脂 + 成员 DRI"),
        ("事实注入", "代码生成事实块，LLM 只负责组织语言"),
    ]
    line(s, 1.1, 2.2, 1.1, 5.85, MINT_2, 2.2)
    for i, (title, desc) in enumerate(steps):
        y = 1.95 + i * 0.98
        oval(s, 0.85, y, 0.5, 0.5, TEAL)
        add_text(s, 0.85, y + 0.12, 0.5, 0.24, str(i + 1), 10, WHITE, True, align=PP_ALIGN.CENTER, font=NUM)
        add_text(s, 1.55, y - 0.02, 5.9, 0.3, title, 13.5, INK, True)
        add_text(s, 1.55, y + 0.34, 5.9, 0.4, desc, 9.6, MUTED)

    rect(s, 8.0, 1.95, 4.55, 2.9, CARD, LINE, radius=0.07)
    add_text(s, 8.25, 2.12, 3.9, 0.35, "清蒸鲈鱼", 17, INK, True)
    add_text(s, 8.25, 2.48, 3.9, 0.22, "每份 · 营养库命中", 8.2, MUTED)
    values = [
        ("180 kcal", "热量"), ("蛋白质 28g", "蛋白"), ("脂肪 6g", "脂肪"),
        ("碳水 2g", "碳水"), ("钠 420mg", "钠"), ("糖 0g", "糖"),
    ]
    for i, (value, label) in enumerate(values):
        col, row = i % 3, i // 3
        x = 8.25 + col * 1.4
        y = 2.84 + row * 0.62
        add_text(s, x, y, 1.3, 0.25, value, 11.5, INK, True, font=NUM)
        add_text(s, x, y + 0.27, 1.3, 0.18, label, 7.5, MUTED)
    rect(s, 8.25, 4.12, 4.05, 0.42, AMBER_SOFT, None, radius=0.08)
    add_text(s, 8.4, 4.19, 3.8, 0.26, "王奶奶（高血压）→ 钠偏高", 8.8, AMBER, True)
    add_text(s, 8.25, 4.62, 4.0, 0.18, "判定：单道菜 ≥ 日上限 40%", 7.2, MUTED)

    add_text(
        s,
        M,
        5.98,
        CW,
        0.4,
        "不让大模型「算」营养，只让它把算好的事实说人话。",
        17,
        TEAL,
        True,
        align=PP_ALIGN.CENTER,
    )
    add_text(
        s,
        M,
        6.52,
        CW,
        0.22,
        "66 道菜 · 59/59 基准菜名覆盖 · 6 项/份 · 阈值：钠 2000mg / 糖 50g / 脂肪 70g",
        8.2,
        "8A9C99",
        align=PP_ALIGN.CENTER,
    )

    footer(s, page)
    notes(
        s,
        "营养这块我们做了一个很重要的工程决策：事实和话术分离。"
        "菜名先归一到营养库规范名，每份 6 项营养素由代码查询，再结合家庭成员档案和慢病阈值做规则计算。"
        "最后把算好的事实注入提示词，大模型只负责组织语言，不负责编造数值。"
        "这样营养数据可追溯，也避免了大模型幻觉。",
    )


def slide_proactive_llm(prs, page):
    s = new_slide(prs)
    header(
        s,
        "11 / 关键技术③",
        "LLM 主动交互：会开口，也会闭嘴",
        "感知冲突 → 事实块 → LLM 组织话术 → 主动播报；用户一开口就并入对话",
    )

    steps = [
        ("感知冲突", "菜品命中成员禁忌或慢病阈值"),
        ("事实块", "菜名、营养、风险、成员，由代码给出"),
        ("LLM 话术", "结合儿童 / 老人 / 慢病语气组织提醒"),
        ("主动播报", "从待机发起，不打断用户说话"),
        ("可打断", "2.5s 合并窗，用户先开口则并入本轮"),
    ]
    line(s, 1.1, 2.2, 1.1, 5.9, MINT_2, 2.2)
    for i, (title, desc) in enumerate(steps):
        y = 1.95 + i * 0.82
        oval(s, 0.85, y, 0.5, 0.5, TEAL)
        add_text(s, 0.85, y + 0.12, 0.5, 0.24, str(i + 1), 10, WHITE, True, align=PP_ALIGN.CENTER, font=NUM)
        add_text(s, 1.55, y - 0.02, 5.9, 0.3, title, 13.5, INK, True)
        add_text(s, 1.55, y + 0.34, 5.9, 0.4, desc, 9.6, MUTED)

    rect(s, 8.0, 1.95, 4.55, 3.45, CARD, LINE, radius=0.07)
    add_text(s, 8.25, 2.12, 3.9, 0.22, "它会主动说", 9, TEAL, True)
    add_text(
        s,
        8.25,
        2.45,
        3.95,
        1.0,
        "“王奶奶，这盘红烧肉的钠偏高，您今天已经接近上限了，建议少夹两块。”",
        13.5,
        INK,
        True,
        line_spacing=1.25,
    )
    line(s, 8.25, 3.58, 12.3, 3.58, "E8F0ED", 0.7)
    tags = [
        "成员：王奶奶 / 高血压",
        "菜品：红烧肉 / 钠 820mg",
        "语气：克制温和",
    ]
    for i, text in enumerate(tags):
        add_text(s, 8.25, 3.75 + i * 0.32, 3.9, 0.22, text, 8.6, MUTED)
    line(s, 8.25, 4.78, 12.3, 4.78, "E8F0ED", 0.7)
    add_text(s, 8.25, 4.95, 3.9, 0.25, "不需要时：只输出（沉默），不打扰。", 8.6, AMBER, True)

    rect(s, M, 6.12, CW, 0.7, DARK, None, radius=0.06)
    add_text(s, M + 0.25, 6.24, 7.2, 0.28, "代码决定该不该说，LLM 决定怎么说。", 11.5, WHITE, True)
    add_text(s, M + 0.25, 6.53, 7.2, 0.2, "主动提醒误触发设计目标 <10%", 7.8, "AFC4C1")
    add_text(s, 8.4, 6.25, 4.0, 0.28, "先有事实，再有话术；不推送，只提醒。", 9.5, TEAL_BRIGHT, True, align=PP_ALIGN.RIGHT)

    footer(s, page)
    notes(
        s,
        "这一页讲 LLM 的主动交互。不是等用户问，而是感知到冲突后，由代码先算出事实，再让 LLM 结合成员语气组织一句提醒。"
        "比如王奶奶高血压，红烧肉钠偏高，LLM 会用克制温和的语气说出来。"
        "用户一开口，2.5 秒合并窗会把提醒并入本轮对话；不需要提醒时，模型只输出（沉默）。"
        "一句话：代码决定该不该说，LLM 决定怎么说。",
    )


def slide_embodied(prs, page):
    s = new_slide(prs)
    header(
        s,
        "12 / 关键技术④",
        "会说话，也会表达",
        "语音 → Agent → 工具 → 动作 → 回应，形成具身闭环",
    )

    nodes = [
        ("用户语音", "发声"),
        ("VAD / ASR", "判句"),
        ("Agent", "编排"),
        ("工具调用", "决策"),
        ("动作 / 播报", "回应"),
    ]
    for i, (title, sub) in enumerate(nodes):
        x = M + i * 1.32
        rect(s, x, 1.95, 1.15, 0.78, CARD if i not in (2, 3) else MINT, LINE, radius=0.08)
        add_text(s, x + 0.05, 2.04, 1.05, 0.25, title, 8.6, INK, True, align=PP_ALIGN.CENTER)
        add_text(s, x + 0.05, 2.37, 1.05, 0.2, sub, 7.4, MUTED, align=PP_ALIGN.CENTER)
        if i < 4:
            arrow(s, x + 1.18, 2.28, 0.13, 0.12, TEAL_BRIGHT)

    # Conversation sample
    add_text(s, 0.72, 3.3, 1.6, 0.2, "用户", 7.5, MUTED)
    add_text(s, 3.1, 3.3, 1.6, 0.2, "工具调用", 7.5, MUTED)
    add_text(s, 5.85, 3.3, 1.6, 0.2, "语音 + 动作", 7.5, MUTED)
    rect(s, 0.72, 3.55, 1.9, 0.55, MINT, MINT_2, radius=0.25)
    add_text(s, 0.84, 3.68, 1.7, 0.25, "把爪子合上", 10, INK, True, align=PP_ALIGN.CENTER)
    arrow(s, 2.72, 3.72, 0.26, 0.12, TEAL_BRIGHT)
    rect(s, 3.1, 3.55, 2.25, 0.55, DARK, None, radius=0.1)
    add_text(s, 3.2, 3.68, 2.05, 0.25, "robot_claw(close)", 8.8, WHITE, True, align=PP_ALIGN.CENTER, font=MONO)
    arrow(s, 5.47, 3.72, 0.26, 0.12, TEAL_BRIGHT)
    rect(s, 5.85, 3.55, 1.95, 0.55, AMBER_SOFT, "EBC9A6", radius=0.2)
    add_text(s, 5.95, 3.68, 1.75, 0.25, "好的，我收起来了。", 9.5, INK, True, align=PP_ALIGN.CENTER)

    rect(s, 8.0, 1.95, 4.6, 3.55, CARD, LINE, radius=0.07)
    add_text(s, 8.25, 2.12, 4.0, 0.3, "安全边界", 13.5, INK, True)
    bullets = [
        "白名单：只暴露 4 个工具（表情 / 夹爪 / 移动 / 档案）",
        "参数在工具边界校验：方向、速度、舵机角度",
        "移动固定短脉冲，到点自动 stop",
        "启动 / 退出清运动目标，失效安全",
        "默认拒绝 → 机器人工具显式 ALLOW",
    ]
    add_block(
        s,
        8.25,
        2.55,
        4.05,
        2.8,
        [{"text": b, "size": 9.0, "color": MUTED, "bullet": "•  ", "bullet_color": TEAL, "after": 10, "spacing": 1.08} for b in bullets],
    )
    add_text(s, 8.25, 4.78, 4.0, 0.28, "原则：宁可不动，不可乱动。", 10, TEAL, True)

    add_text(s, M, 5.68, CW, 0.28, "工具调用必须同时说一句话；动作失败不阻塞语音回路。", 10.5, INK_SOFT, True)
    pill(s, M, 6.22, 4.3, 0.32, "下一步：动作 → 拍照 → VL 确认，形成可观测闭环", MINT, TEAL, 8, line=MINT_2)

    footer(s, page)
    notes(
        s,
        "具身交互是 NutriBot 和普通语音助手最大的区别。"
        "用户说「把爪子合上」，ASR 转文字，Agent 决定调用 robot_claw，MCU 执行动作，同时语音回应。"
        "右边是安全边界：白名单、参数校验、短脉冲自动停、启动退出清运动目标、权限默认拒绝。"
        "我们坚持一个原则：工具调用必须同时说话，不能只动不说。",
    )


def slide_metrics(prs, page):
    s = new_slide(prs)
    header(
        s,
        "13 / 实测与质量",
        "能演示，也能持续跑",
        "截至 2026-10 的板上实测；识别率是下一阶段主攻方向",
    )

    metrics = [
        ("语音闭环", "全通", "唤醒 → ASR → LLM → TTS"),
        ("多菜识别", "5/5", "单帧最多 8 道"),
        ("营养覆盖", "59/59", "66 道菜 · 6 项/份"),
        ("主动时延", "3.3s", "3B 轻量档；8B 单帧约 16s"),
        ("工程质量", "101 项", "单元测试 · 分层架构"),
        ("稳定运行", "常驻", "二次 Ctrl-C 强退"),
    ]
    for i, (label, value, sub) in enumerate(metrics):
        col, row = i % 2, i // 2
        x = M + col * 3.7
        y = 1.95 + row * 1.3
        rect(s, x, y, 3.55, 1.15, CARD, LINE, radius=0.07)
        add_text(s, x + 0.2, y + 0.14, 3.1, 0.2, label, 8.8, MUTED, True)
        add_text(s, x + 0.2, y + 0.4, 3.1, 0.35, value, 19, TEAL, True, font=NUM)
        add_text(s, x + 0.2, y + 0.82, 3.1, 0.22, sub, 8.2, INK_SOFT)

    rect(s, 8.2, 1.95, 4.4, 4.0, CARD, LINE, radius=0.07)
    add_text(s, 8.45, 2.1, 3.9, 0.25, "下一步攻坚", 13.5, INK, True)

    next_steps = [
        ("01", "识别", "闭集检测器 + 自采数据 + 现有标注"),
        ("02", "份量", "RGB-D 深度 + 分割，目标误差 <25%"),
        ("03", "体验", "跨天趋势 / 周报 / 多成员"),
    ]
    for i, (num, title, desc) in enumerate(next_steps):
        y = 2.55 + i * 0.88
        oval(s, 8.45, y, 0.36, 0.36, MINT)
        add_text(s, 8.45, y + 0.07, 0.36, 0.2, num, 7.2, TEAL, True, align=PP_ALIGN.CENTER, font=NUM)
        add_text(s, 8.95, y - 0.02, 3.4, 0.26, title, 11.5, INK, True)
        add_text(s, 8.95, y + 0.28, 3.4, 0.5, desc, 8.8, MUTED, line_spacing=1.08)

    line(s, 8.45, 5.25, 12.35, 5.25, "E8F0ED", 0.7)
    add_text(s, 8.45, 5.42, 3.9, 0.35, "多菜实景已稳定 5/5；开放场景继续优化。", 8.4, INK_SOFT, True)

    add_text(
        s,
        M,
        6.35,
        CW,
        0.22,
        "多菜实景已稳定；下一步用闭集检测器提升开放场景识别稳定性。",
        8.2,
        "8A9C99",
    )

    footer(s, page)
    notes(
        s,
        "这一页讲已经跑通的能力：语音闭环、多菜识别、营养覆盖、主动时延和工程质量。"
        "多菜实景 5/5，非餐桌负样本 0 误报，营养库 59/59 覆盖。"
        "下一步主攻识别稳定性：训练闭集检测器，用自采数据和现有标注补上；同时做 RGB-D 份量估算和跨天周报。",
    )


def slide_scenarios(prs, page):
    s = new_slide(prs)
    header(
        s,
        "14 / 应用场景",
        "从一张餐桌，到一个健康网络",
        "先家庭验证，再机构复制，最后行业方案",
    )

    scenarios = [
        ("家庭餐桌", "首发", "老人控盐、孩子食育、慢病成员忌口；无感记录每一餐。", "价值：高频刚需，决策链短", "谁：有老人或慢病成员的家庭"),
        ("社区 / 养老", "试点", "集体用餐巡视、高风险菜品提示、家属远程报告。", "价值：人力紧、风险高、可复制", "谁：社区食堂、养老机构"),
        ("餐厅 / 食堂", "B 端", "菜品营养标签、出品一致性、团餐健康管理。", "价值：标准化、规模化、付费方明确", "谁：连锁餐饮、单位食堂"),
        ("健康管理 / 保险", "生态", "饮食行为数据、干预闭环、按效果付费。", "价值：数据闭环、按效果付费", "谁：健康管理公司、保险公司"),
    ]
    for i, (title, tag, desc, value, who) in enumerate(scenarios):
        col, row = i % 2, i // 2
        x = M + col * 5.93
        y = 1.95 + row * 2.2
        rect(s, x, y, 5.8, 2.05, CARD, LINE, radius=0.07)
        add_text(s, x + 0.25, y + 0.2, 3.8, 0.38, title, 16, INK, True)
        pill(s, x + 4.65, y + 0.2, 0.9, 0.28, tag, MINT, TEAL, 7.5, line=MINT_2)
        add_text(s, x + 0.25, y + 0.74, 5.1, 0.55, desc, 10.2, MUTED, line_spacing=1.12)
        add_text(s, x + 0.25, y + 1.2, 5.1, 0.22, value, 8.5, TEAL)
        add_text(s, x + 0.25, y + 1.65, 5.1, 0.22, who, 8.5, MUTED)

    add_text(
        s,
        M,
        6.45,
        CW,
        0.25,
        "落地路径：100 台家庭种子用户 → 养老 / 社区试点 → 餐饮与保险合作",
        9.5,
        MUTED,
        align=PP_ALIGN.CENTER,
    )

    footer(s, page)
    notes(
        s,
        "应用场景先聚焦家庭，因为这是最高频、最刚需的场景。"
        "家庭跑通后，可以复制到社区养老、餐厅食堂和健康管理/保险。"
        "落地路径是 100 台种子用户、机构试点、再到行业合作，不一开始就铺渠道。",
    )


def slide_market(prs, page):
    s = new_slide(prs)
    header(
        s,
        "15 / 市场潜力",
        "老龄化 × 慢病 × 健康消费，三重顺风",
        "不做所有人的健康，先服务「有健康敏感成员的家庭」",
    )

    stats = [
        ("3.1 亿", "60+ 人口", "2024 年，占比 22.0%"),
        ("4 亿+", "慢病患者", "饮食是首要可干预因素"),
        ("50.7%", "成人超重 / 肥胖", "6–17 岁儿童青少年 19%"),
        ("2.9 亿", "城镇家庭", "健康消费的决策单元"),
    ]
    for i, (num, label, desc) in enumerate(stats):
        col, row = i % 2, i // 2
        x = M + col * 3.0
        y = 1.95 + row * 1.6
        rect(s, x, y, 2.85, 1.45, CARD, LINE, radius=0.07)
        add_text(s, x + 0.2, y + 0.15, 2.5, 0.4, num, 22, TEAL, True, font=NUM)
        add_text(s, x + 0.2, y + 0.62, 2.5, 0.24, label, 9.5, INK, True)
        add_text(s, x + 0.2, y + 0.94, 2.5, 0.35, desc, 8.3, MUTED)

    funnel = [
        (7.1, 5.4, TEAL, "约 1 亿户", "有老人或慢病成员的家庭"),
        (7.6, 4.4, "3E9B96", "3000 万户", "一二线健康敏感家庭"),
        (8.1, 3.4, AMBER, "3 年 0.1%", "3 万台 × 2000 元 ≈ 6000 万元"),
    ]
    for y, w, color, big, small in [
        (1.95, 5.4, TEAL, "约 1 亿户", "有老人或慢病成员的家庭"),
        (3.05, 4.4, "3E9B96", "3000 万户", "一二线健康敏感家庭"),
        (4.15, 3.4, AMBER, "3 年 0.1%", "3 万台 × 2000 元 ≈ 6000 万元"),
    ]:
        x = 7.0 + (5.6 - w) / 2
        rect(s, x, y, w, 0.92, color, None, radius=0.08)
        add_text(s, x + 0.15, y + 0.13, w - 0.3, 0.3, big, 13.5, WHITE, True, align=PP_ALIGN.CENTER)
        add_text(s, x + 0.15, y + 0.53, w - 0.3, 0.2, small, 8, WHITE, align=PP_ALIGN.CENTER)

    rect(s, 7.0, 5.35, 5.6, 0.55, MINT, MINT_2, radius=0.06)
    add_text(s, 7.2, 5.47, 5.2, 0.25, "第二曲线：服务订阅 99–199 元/年", 9.5, TEAL, True, align=PP_ALIGN.CENTER)

    add_text(
        s,
        M,
        6.42,
        CW,
        0.38,
        "数据来源：国家统计局 2024 年统计公报；《中国居民营养与慢性病状况报告（2020 年）》；SOM 为项目组自下而上测算。",
        7.2,
        "8A9C99",
    )

    footer(s, page)
    notes(
        s,
        "市场空间用自下而上的算法，不引用夸张的行业报告。"
        "中国有 3.1 亿 60 岁以上人口、4 亿以上慢病人群，城镇家庭约 2.9 亿户。"
        "我们聚焦有老人或慢病成员的家庭，约 1 亿户；一二线健康敏感家庭约 3000 万户。"
        "三年做到 0.1% 渗透，就是 3 万台、约 6000 万元硬件收入，服务订阅是第二曲线。",
    )


def slide_competition(prs, page):
    s = new_slide(prs)
    header(
        s,
        "16 / 竞争格局",
        "主动 × 垂直 × 具身，三者交集仍是空白",
        "不是和 App 抢记录，而是把「感知入口」前移到餐桌",
    )

    # Quadrants
    rect(s, 1.45, 1.95, 3.0, 1.5, "F3F8F6", None, radius=0.02)
    rect(s, 4.45, 1.95, 3.0, 1.5, "EEF3F1", None, radius=0.02)
    rect(s, 1.45, 3.45, 3.0, 1.55, "F3F8F6", None, radius=0.02)
    rect(s, 4.45, 3.45, 3.0, 1.55, MINT, None, radius=0.02)
    line(s, 1.45, 3.45, 7.45, 3.45, "BBD3CC", 1.0)
    line(s, 4.45, 1.95, 4.45, 5.0, "BBD3CC", 1.0)

    add_text(s, 1.55, 2.02, 2.6, 0.2, "通用 × 被动", 7.5, "8FA7A3")
    add_text(s, 5.8, 2.02, 2.5, 0.2, "通用 × 主动", 7.5, "8FA7A3")
    add_text(s, 1.55, 3.55, 2.6, 0.2, "垂直 × 被动", 7.5, "8FA7A3")
    add_text(s, 5.8, 3.55, 2.5, 0.2, "垂直 × 主动", 7.5, TEAL, True)

    add_text(s, 1.45, 5.12, 1.6, 0.2, "← 被动记录", 8, MUTED)
    add_text(s, 5.52, 5.12, 1.9, 0.2, "主动感知 →", 8, MUTED, align=PP_ALIGN.RIGHT)
    add_text(s, 0.82, 2.9, 1.1, 0.25, "通用助手  →  垂直营养", 7.5, MUTED, rotation=270)

    competitors = [
        (2.3, 2.55, "智能音箱", 2.55, 2.47, PP_ALIGN.LEFT),
        (3.35, 3.02, "智能冰箱", 3.6, 2.94, PP_ALIGN.LEFT),
        (2.4, 4.25, "薄荷健康 / Keep", 2.65, 4.17, PP_ALIGN.LEFT),
    ]
    for cx, cy, label, tx, ty, align in competitors:
        oval(s, cx - 0.08, cy - 0.08, 0.16, 0.16, "9AAEAA")
        add_text(s, tx, ty, 1.8, 0.2, label, 8, MUTED, align=align)

    oval(s, 5.12, 4.42, 0.16, 0.16, "9AAEAA")
    add_text(s, 4.35, 4.72, 1.55, 0.2, "人类营养师", 8, MUTED, align=PP_ALIGN.CENTER)

    oval(s, 6.2, 4.05, 0.5, 0.5, None, TEAL_BRIGHT, 1.2)
    oval(s, 6.3, 4.15, 0.3, 0.3, TEAL)
    add_text(s, 6.05, 4.62, 1.4, 0.25, "NutriBot", 9, TEAL, True)

    add_text(s, 8.0, 1.95, 4.5, 0.3, "三个差异", 9.5, TEAL, True)
    diffs = [
        ("01", "感知差异", "不要求用户记录，设备主动理解一餐"),
        ("02", "知识差异", "营养库 + 家庭档案 + 规则计算，事实可追溯"),
        ("03", "交互差异", "语音 + 表情 + 动作，是在场的具身伙伴"),
    ]
    for i, (num, title, desc) in enumerate(diffs):
        y = 2.4 + i * 1.05
        oval(s, 8.0, y, 0.38, 0.38, MINT)
        add_text(s, 8.0, y + 0.08, 0.38, 0.22, num, 7.5, TEAL, True, align=PP_ALIGN.CENTER, font=NUM)
        add_text(s, 8.52, y - 0.02, 4.0, 0.3, title, 11.5, INK, True)
        add_text(s, 8.52, y + 0.32, 4.0, 0.45, desc, 9.2, MUTED, line_spacing=1.08)

    rect(s, M, 5.7, CW, 0.78, DARK, None, radius=0.06)
    add_text(
        s,
        M + 0.25,
        5.85,
        CW - 0.5,
        0.25,
        "壁垒：端侧多模态工程 × 家庭营养数据 × 具身交互",
        11,
        WHITE,
        True,
        align=PP_ALIGN.CENTER,
    )

    footer(s, page)
    notes(
        s,
        "竞争格局用二维矩阵看：横轴是从被动记录到主动感知，纵轴是从通用助手到垂直营养。"
        "手机 App 和营养师在垂直但被动，智能音箱和冰箱在通用，NutriBot 在主动、垂直、具身这个交集。"
        "我们的壁垒不是单点模型，而是端侧多模态工程、家庭营养数据和具身交互三者的组合。",
    )


def slide_business(prs, page):
    s = new_slide(prs)
    header(
        s,
        "17 / 商业模式",
        "硬件入口 + 服务订阅 + B 端方案",
        "先用硬件进入家庭，再用服务延长生命周期",
    )

    revenue = [
        ("硬件销售", "1500–2500 元 / 台", "目标 BOM < 800 元，毛利率约 45–50%"),
        ("服务订阅", "99–199 元 / 年", "周报、趋势、多成员、专家内容"),
        ("B 端方案", "按点位 / 项目", "养老、社区食堂、保险健康管理"),
    ]
    for i, (title, value, desc) in enumerate(revenue):
        x = M + i * 4.04
        rect(s, x, 1.95, 3.8, 1.45, CARD, LINE, radius=0.07)
        add_text(s, x + 0.22, 2.1, 3.3, 0.22, title, 9.5, MUTED, True)
        add_text(s, x + 0.22, 2.38, 3.3, 0.35, value, 17, TEAL, True)
        add_text(s, x + 0.22, 2.88, 3.3, 0.4, desc, 8.8, INK_SOFT, line_spacing=1.06)

    rect(s, M, 3.65, CW, 0.82, DARK, None, radius=0.06)
    cells = [
        ("售价", "1999 元"), ("BOM", "780 元"), ("渠道 / 售后", "300 元"), ("毛利", "约 900 元（45%）"),
    ]
    for i, (label, value) in enumerate(cells):
        x = M + 0.25 + i * 2.95
        add_text(s, x, 3.78, 2.7, 0.2, label, 7.8, "AFC4C1")
        add_text(s, x, 4.0, 2.7, 0.28, value, 11, WHITE, True)
        if i < 3:
            line(s, x + 2.78, 3.78, x + 2.78, 4.32, "2C5359", 0.7)

    gtm = [
        ("0–6 个月", "100 台种子用户，打磨家庭场景"),
        ("6–12 个月", "众筹 / 电商首发，养老机构试点"),
        ("12–24 个月", "区域渠道，保险 / 健康管理合作"),
    ]
    for i, (phase, desc) in enumerate(gtm):
        x = M + i * 4.04
        rect(s, x, 4.9, 3.8, 1.2, CARD, LINE, radius=0.07)
        add_text(s, x + 0.22, 5.04, 3.3, 0.22, phase, 9.5, AMBER, True)
        add_text(s, x + 0.22, 5.38, 3.35, 0.55, desc, 9.8, INK_SOFT, line_spacing=1.08)

    add_text(
        s,
        M,
        6.45,
        CW,
        0.22,
        "收入结构目标：硬件 70% → 服务 30%；服务收入提升 LTV。",
        8.5,
        MUTED,
    )

    footer(s, page)
    notes(
        s,
        "商业模式是硬件入口加服务订阅加 B 端方案。硬件定价 1500 到 2500，BOM 控制在 800 以内，毛利率约 45%。"
        "服务订阅 99 到 199 一年，提供周报、趋势和多成员能力。B 端面向养老、社区食堂和保险。"
        "节奏是种子用户、众筹电商、机构合作，收入结构从硬件为主逐步转向服务。",
    )


def slide_social(prs, page):
    s = new_slide(prs)
    header(
        s,
        "18 / 社会价值",
        "让健康提醒，发生在饭桌上",
        "而不是体检报告里",
    )

    values = [
        ("01", "慢病预防前移", "每餐提醒高盐、高糖、高脂；把干预从「事后治疗」前移到「每餐选择」。", "影响：高盐高糖暴露可被逐餐看见"),
        ("02", "积极老龄化", "陪伴 + 独立生活支持；让老人不必依赖子女也能吃得更健康。", "影响：独立生活能力 + 陪伴"),
        ("03", "儿童食育", "在餐桌上建立食物与营养的认知；让「好好吃饭」变成可解释的习惯。", "影响：营养认知从小建立"),
        ("04", "隐私与普惠", "端侧可全离线，数据不出户；目标价格带进入普通家庭。", "影响：数据不出户，价格可负担"),
    ]
    for i, (num, title, desc, impact) in enumerate(values):
        col, row = i % 2, i // 2
        x = M + col * 5.93
        y = 1.95 + row * 2.1
        rect(s, x, y, 5.8, 1.95, CARD, LINE, radius=0.07)
        oval(s, x + 0.25, y + 0.25, 0.42, 0.42, MINT)
        add_text(s, x + 0.25, y + 0.31, 0.42, 0.22, num, 8.5, TEAL, True, align=PP_ALIGN.CENTER, font=NUM)
        add_text(s, x + 0.82, y + 0.22, 4.7, 0.32, title, 15, INK, True)
        add_text(s, x + 0.82, y + 0.68, 4.65, 0.55, desc, 9.8, MUTED, line_spacing=1.12)
        add_text(s, x + 0.82, y + 1.38, 4.65, 0.22, impact, 8.5, TEAL)

    add_text(
        s,
        M,
        6.28,
        CW,
        0.5,
        "科技的温度，是让健康提醒发生在饭桌上，而不是体检报告里。",
        18,
        TEAL,
        True,
        align=PP_ALIGN.CENTER,
    )

    footer(s, page)
    notes(
        s,
        "社会价值有四层：慢病预防前移、积极老龄化、儿童食育、隐私与普惠。"
        "最核心的是把干预从体检报告前移到每一餐。"
        "同时坚持端侧部署，数据不出户，目标价格带让普通家庭用得起。",
    )


def slide_roadmap(prs, page):
    s = new_slide(prs)
    header(
        s,
        "19 / 路线图",
        "从能演示，到能复制",
        "三个阶段，把工程样机推向小批量验证",
    )

    phases = [
        ("已完成", "2026 Q3–Q4", "已交付", "端到端闭环跑通", [
            "语音闭环：唤醒 / ASR / LLM / TTS",
            "主动看菜 + 场景状态机",
            "营养库 / 餐账 / 家庭档案",
            "机器人动作 + 8B 模型升级",
        ]),
        ("未来 3 个月", "2026 Q4–2027 Q1", "进行中", "85% 识别 / 100 台", [
            "闭集检测器，识别率目标 85%",
            "RGB-D 份量估算",
            "跨天趋势 / 周报",
            "外壳、稳定性、100 台种子用户",
        ]),
        ("6–12 个月", "2027", "规划", "小批量试产", [
            "身份识别（待官方模型）",
            "机构版：养老 / 社区食堂",
            "小批量试产",
            "渠道 / 保险合作",
        ]),
    ]
    for i, (title, period, status, milestone, items) in enumerate(phases):
        x = M + i * 4.04
        rect(s, x, 1.95, 3.8, 3.65, CARD, LINE, radius=0.07)
        rect(s, x, 1.95, 3.8, 0.07, [TEAL, TEAL_BRIGHT, AMBER][i], None, radius=0.05)
        add_text(s, x + 0.22, 2.15, 2.4, 0.3, title, 14.5, INK, True)
        add_text(s, x + 0.22, 2.52, 2.2, 0.2, period, 8, AMBER, True)
        pill(s, x + 2.65, 2.12, 0.92, 0.26, status, MINT, TEAL, 7.2, line=MINT_2)
        add_block(
            s,
            x + 0.22,
            2.9,
            3.35,
            2.0,
            [{"text": item, "size": 9.4, "color": MUTED, "bullet": "•  ", "bullet_color": TEAL, "after": 11, "spacing": 1.06} for item in items],
        )
        pill(s, x + 0.22, 4.98, 3.3, 0.32, "里程碑：" + milestone, MINT, TEAL, 7.8, line=MINT_2)

    rect(s, M, 5.85, CW, 0.8, DARK, None, radius=0.06)
    add_text(s, M + 0.25, 5.96, 2.2, 0.2, "项目书目标", 7.5, "AFC4C1")
    add_text(
        s,
        M + 0.25,
        6.18,
        CW - 0.5,
        0.25,
        "识别率 >85%   |   份量误差 <25%   |   主动误触发 <10%   |   端到端 <3s",
        10.5,
        WHITE,
        True,
        align=PP_ALIGN.CENTER,
    )

    footer(s, page)
    notes(
        s,
        "路线图分三段。第一阶段已经完成语音闭环、主动看菜、场景状态机、营养库和机器人动作。"
        "未来三个月主攻识别率，训练闭集检测器，同时做 RGB-D 份量估算、跨天周报和 100 台种子用户。"
        "六到十二个月做身份识别、机构版和小批量试产。项目书目标是识别率 85%、份量误差 25%、误触发 10%、端到端 3 秒。",
    )


def slide_closing(prs):
    s = new_slide(prs, DARK)
    oval(s, 10.2, 0.8, 3.8, 3.8, DARK_2)
    oval(s, 10.55, 1.15, 3.1, 3.1, DARK_3)
    oval(s, 10.95, 1.55, 2.3, 2.3, None, TEAL, 1.0)
    oval(s, 11.45, 2.05, 1.3, 1.3, None, "2C5A5F", 0.8)

    pill(s, 0.78, 0.8, 3.4, 0.35, "NutriBot · 心屿科技", TEAL, WHITE, 9.5)
    add_text(s, 0.78, 2.05, 8.2, 0.8, "让每一顿饭，都被看见。", 36, WHITE, True, line_spacing=1.0)
    line(s, 0.82, 3.05, 4.2, 3.05, TEAL_BRIGHT, 2.0)
    add_text(s, 0.82, 3.28, 7.8, 0.45, "NutriBot 营养管家 · 餐桌旁的端侧 AI 健康伙伴", 16, TEAL_BRIGHT, True)
    add_text(
        s,
        0.82,
        4.0,
        7.4,
        0.6,
        "本地化部署 × 语音全链路 × LLM 看菜对话 × 具身表达",
        10.5,
        "C9D8D5",
    )

    pill(s, 0.82, 5.25, 1.25, 0.42, "Q & A", AMBER, WHITE, 11)
    add_text(s, 0.82, 6.3, 7.5, 0.25, "心屿科技 · 2026 高通具身智能与机器人大赛", 9.5, "8FA7A3")
    add_text(s, 0.82, 6.62, 7.5, 0.22, "项目联系人：方家挺 / 齐二波", 8.5, "6F8683")

    notes(
        s,
        "最后一页，回到我们的初心：让每一顿饭，都被看见。"
        "NutriBot 用本地化部署、语音全链路、LLM 看菜对话和具身表达，把健康提醒带到餐桌上。"
        "谢谢各位评委，欢迎提问。",
    )


def slide_appendix_benchmark(prs):
    s = new_slide(prs)
    header(
        s,
        "A1 / 附录",
        "已验能力与测试方法",
        "多菜实景、负样本、营养覆盖、时延与语音闭环；数据截至 2026-10",
    )

    rows = [
        ["测试项", "方法", "结果", "说明"],
        ["多菜实景", "现场连续帧", "5/5 全中", "单帧最多 8 道"],
        ["非餐桌负样本", "空桌、人、墙", "0 误报", "开放命名"],
        ["营养库覆盖", "59 个基准菜名", "59/59", "66 道菜总库"],
        ["主动链路", "放菜 → 播报", "3.3s（3B 轻量档）", "8B 单帧约 16s"],
        ["语音闭环", "唤醒 → ASR → LLM → TTS", "全通", "常驻回路"],
        ["工程质量", "单元测试", "101 项", "分层架构"],
    ]
    add_table(s, M, 1.95, CW, [2.2, 3.1, 3.4, 3.19], rows, row_h=0.52, font_size=9.6)
    add_text(
        s,
        M,
        5.75,
        CW,
        0.5,
        "识别稳定性持续优化：下一阶段训练闭集检测器，保留 VL 作为兜底。",
        9.5,
        INK_SOFT,
        True,
    )
    add_text(
        s,
        M,
        6.35,
        CW,
        0.3,
        "测试脚本与标注随仓库维护；数字会随模型版本更新，答辩前请以最新实测为准。",
        8,
        "8A9C99",
    )

    footer(s, "A1")
    notes(
        s,
        "附录页给技术评委看已验能力。多菜实景 5/5；非餐桌负样本 0 误报；营养库覆盖 59/59；"
        "主动链路 3B 轻量档 3.3 秒；语音闭环全通；101 项单元测试。"
        "识别稳定性下一阶段用闭集检测器继续提升。",
    )


def slide_appendix_risk(prs):
    s = new_slide(prs)
    header(
        s,
        "A2 / 附录",
        "关键风险与应对",
        "答辩时主动说明边界，比回避更有说服力",
    )

    rows = [
        ["风险", "影响", "应对"],
        ["开放场景识别稳定性", "演示与体验", "闭集检测器 + 自采数据 + 现有标注；VL 兜底"],
        ["份量估算依赖 RGB-D", "热量误差", "先按「每份」口径；后续深度 + 分割，目标误差 <25%"],
        ["人脸 / 身份识别缺官方模型", "个性化受限", "当前所有已建档成员参与提醒；等 Model Farm 上架"],
        ["隐私与合规", "用户信任", "端侧推理、数据不出户；不做医疗诊断，明确免责声明"],
        ["成本与供应链", "毛利与量产", "现成件样机；BOM < 800；小批量试产验证"],
    ]
    add_table(s, M, 1.95, CW, [3.0, 3.2, 5.69], rows, row_h=0.62, font_size=9.4)

    add_text(
        s,
        M,
        5.95,
        CW,
        0.3,
        "原则：先把能做的做到稳定，再把不能做的讲清楚。",
        10,
        TEAL,
        True,
        align=PP_ALIGN.CENTER,
    )

    footer(s, "A2")
    notes(
        s,
        "这页是风险应对。识别稳定性、份量、人脸、隐私、成本，这五个问题评委大概率会问。"
        "我们的原则是先把能做的做到稳定，再把不能做的讲清楚。"
        "比如人脸识别，官方模型库里没有可用的 embedding 模型，我们就先不做身份区分，所有已建档成员参与提醒。",
    )


def build():
    prs = Presentation()
    prs.slide_width = Inches(SW)
    prs.slide_height = Inches(SH)

    slide_cover(prs)
    slide_overview(prs, 2)
    slide_pain(prs, 3)
    slide_gap(prs, 4)
    slide_definition(prs, 5)
    slide_hardware(prs, 6)
    slide_experience(prs, 7)
    slide_features(prs, 8)
    slide_architecture(prs, 9)
    slide_model_stack(prs, 10)
    slide_proactive(prs, 11)
    slide_nutrition(prs, 12)
    slide_proactive_llm(prs, 13)
    slide_embodied(prs, 14)
    slide_metrics(prs, 15)
    slide_scenarios(prs, 16)
    slide_market(prs, 17)
    slide_competition(prs, 18)
    slide_business(prs, 19)
    slide_social(prs, 20)
    slide_roadmap(prs, 21)
    slide_closing(prs)
    slide_appendix_benchmark(prs)
    slide_appendix_risk(prs)

    out_dir = Path(__file__).resolve().parent.parent / "docs" / "nutrition_robot"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / "NutriBot_项目介绍_路演版.pptx"
    prs.save(out)
    print(f"saved {out} ({out.stat().st_size / 1024:.0f} KB, {len(prs.slides)} slides)")


if __name__ == "__main__":
    build()
