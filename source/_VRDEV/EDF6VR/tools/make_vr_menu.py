"""Builds the layouts of the in-game "VR setting" menu into assets/ui/ (build inputs, regenerated
from the installed game; never published), one pair a display language:

  LYT_PAUSEMENU.SGO / LYT_PAUSEMENU.JA.SGO
        the game's Start (pause) menu layout plus one button, ButtonVrSetting, at the end of each
        of its three button lists (offline, host, room member)
  LYT_EDF6VR_SETTING.SGO / LYT_EDF6VR_SETTING.JA.SGO
        the window that button opens: a plain HUiLayout, one button a setting (its name) and one
        text field a setting (its value now; the plugin writes it), and under them one more
        button that only says where the other settings are

  python -B tools/make_vr_menu.py [out-dir]

Read-only on the game: the layouts are read from Root.cpk and written into this project only. The
plugin embeds all four (src/menu_assets.rc) and writes the pair of the game's language (Japanese,
or English for every other) to Mods/UI while it runs (src/vr_menu.h); EDFModLoader's Mods folder
redirector makes the game load them. ROWS here and kVrMenuItems in src/vr_menu.h are the same list
in the same order; src/vr_menu_rows.inc, written here, carries the count for the plugin to check.
"""
import hashlib, os, pathlib, sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / 'edf6'))
import cpk          # noqa: E402
import sgo_write    # noqa: E402
from sgo_write import Arr, Int, Flt, Str  # noqa: E402

GAME = HERE.parents[2]
try:
    import crilayla  # noqa: E402  (EDF6VR keeps its own copy beside cpk.py when it has one)
except ImportError:
    sys.path.insert(0, str(GAME / '_MultislotDEV' / 'tools'))
    import crilayla  # noqa: E402

# The button on the Start menu and the window's title, English and Japanese.
MENU_LABEL = ('VR setting', 'VR設定')
# The line under the rows (the user's wording, 2026-10-12: "一番下部に、その他の項目はEDF6 VR setting.exeを
# 使用して下さい。って書いといて"; then "読みにくいので他の項目と同じように背景つけて"): a row like the others, a
# button with the rows' own bar behind its text, that changes nothing when pressed (item -1).
NOTE = ('For other settings, use EDF6 VR setting.exe.',
        'その他の項目はEDF6 VR setting.exeを使用して下さい。')

# One row a setting, in the order of kVrMenuItems (src/vr_menu.h): its name, English and Japanese.
ROWS = [
    ('Scope zoom', 'スコープのズーム'),
    ('Air-strike camera', '空爆カメラ'),
    ('Stick up / down buttons', '右スティック上下をYとL3に置き換え'),
    ('Recoil', '射撃の反動（見た目のみ影響）'),
    ('Shot vibration', '射撃時の振動'),
    ('Aim mark size', '照準マークの大きさ'),
    ('Compact HUD', 'コンパクトHUD'),
    ('Desktop mirror', 'モニターに配信用ミラーを表示'),
    ('Laser sight thickness', 'レーザーサイトの太さ'),
    ('Throw guide thickness', '投擲ガイドの太さ'),
    ('Vehicle aim line thickness', '乗り物の照準線の太さ'),
    ('Hand aim: Nix', 'モーションコントロール照準：ニクス'),
    ('Hand aim: Depth Crawler', 'モーションコントロール照準：デプスクローラー'),
    ('Hand aim: Barga', 'モーションコントロール照準：バルガ'),
    ('Hand aim: Tank', 'モーションコントロール照準：戦車'),
    ('Hand aim: Combat vehicle', 'モーションコントロール照準：戦闘車両'),
    ('Hand aim: Helicopter', 'モーションコントロール照準：ヘリ'),
    ('Hand aim: Gun seat', 'モーションコントロール照準：銃座'),
    ('Hand aim: Brute door gun', 'モーションコントロール照準：ブルートの機銃'),
]

# The window, in the game's 1920 x 1080 menu space. A row is its height and the gap above it.
ROW_HEIGHT, ROW_GAP = 36.0, 6.0
LEFT, TOP = 170.0, 120.0
# The buttons are as wide as the line under the rows needs (the Japanese one is 670 wide at this size and
# starts 65 in from the bar's edge). All the same width: the column scrolls, so one wider child put a
# scroll bar over the list and was cut at the column's edge (seen on the virtual headset, 2026-10-12).
NAME_WIDTH, VALUE_WIDTH, BETWEEN = 780.0, 440.0, 24.0


def button_name(i):
    return 'VrBtn%02d' % i


def value_name(i):
    return 'VrVal%02d' % i


def game_file(archive, name):
    entry = archive.index[('UI', name)]
    with open(archive.path, 'rb') as handle:
        handle.seek(archive.base + int(entry['FileOffset']))
        data = handle.read(int(entry['FileSize']))
    if int(entry['ExtractSize']) != int(entry['FileSize']):
        data = crilayla.decompress(data)
    return data


def checked(data, what):
    values = sgo_write.parse(data)
    if sgo_write.write(values) != data:
        raise SystemExit(f'the writer does not reproduce UI/{what}; refusing to build')
    return values


def pause_menu(data, language):
    values = sgo_write.parse(data)
    table = dict(values)
    if 'ButtonVrSetting' in table:
        raise SystemExit('the pause layout already has ButtonVrSetting')
    option = table['ButtonOption']          # [[0, 'Button02Base'], [[text ...], [callback ...]]]
    assert option.kind == 'arr' and len(option.value) == 2
    button = Arr(option.value[0], Arr(
        Arr(Str('text_dr'), Str(MENU_LABEL[language])),
        Arr(Str('callback'), Arr(Str('CallWindow'), Arr(
            Arr(Str('sgo_path'), Str('app:/UI/lyt_EDF6VR_Setting.sgo')),
            Arr(Str('result_callback'), Str('cbReceiveResultFromMsgBox')))))))
    # Five buttons in the frame that held four: each a little closer to the one above it
    # (28 -> 14 of the 42 + 28 a button took), so that the last sits where the fourth sat.
    base = table['Button02Base']
    assert base.value[2] == Arr(Flt(0.0), Flt(28.0), Flt(0.0)) and base.value[3].value[3] == Flt(42.0)
    base.value[2] = Arr(Flt(0.0), Flt(14.0), Flt(0.0))
    for tree in ('offline', 'on_host', 'on_member'):
        node = table[tree]                  # [['ButtonsSpace'], [[name], 0, [name], 0, ...]]
        assert node.kind == 'arr' and len(node.value) == 2 and node.value[1].kind == 'arr'
        assert node.value[1].value[-2] == Arr(Str('ButtonOption')), tree
        node.value[1].value.extend([Arr(Str('ButtonVrSetting')), Int(0)])
    return values + [('ButtonVrSetting', button)]


def text_field(pos, area, flags, text, size):
    return Arr(Str('TextField'), Str('app:/UI/Transparent_skin.sgo'),
               Arr(*[Flt(v) for v in pos]), Arr(*[Flt(v) for v in area]),
               Arr(*[Int(v) for v in flags]), Int(0), Int(0),
               Arr(Arr(Str('text_dr'), Str(text)), Arr(Str('font_size'), Arr(Int(size), Int(size))),
                   Arr(Str('font_border_width'), Flt(2.5))))


def setting_window(data, language):
    """From the game's own two-button chooser (lyt_OnOffSelect, a plain HUiLayout): its frame, its
    window skin, its button style and its cancel actions, with our rows in place of its buttons."""
    src = dict(sgo_write.parse(data))
    keep = ['BaseFrame', 'Window', 'cancel_action', 'default_cancel_action', 'exit_cancel_action',
            'transition', 'transition_scale', 'transition_slide', 'xgs_ui_object_class']
    out = [(name, src[name]) for name in keep]
    assert src['xgs_ui_object_class'] == Str('HUiLayout')
    rows = len(ROWS)
    height = rows * (ROW_HEIGHT + ROW_GAP) + ROW_GAP
    column_height = height + ROW_HEIGHT + ROW_GAP        # the rows and the line under them
    # The backdrop: the chooser's window skin, placed as its description box is (a derived
    # component with pos, area and flag_layout of its own). The skin is drawn about pos, its
    # centre (seen on the virtual headset, 2026-10-12: given the corner, the box covered only the
    # upper left quarter of the menu), so pos is the middle of the menu.
    width = NAME_WIDTH + BETWEEN + VALUE_WIDTH + 80
    depth = column_height + 120
    out.append(('VrWindow', Arr(Arr(Int(0), Str('Window')), Arr(
        Arr(Str('pos'), Arr(Flt(LEFT - 40 + width / 2), Flt(TOP - 80 + depth / 2), Flt(0.0), Flt(0.0))),
        Arr(Str('area'), Arr(Flt(0.0), Flt(0.0), Flt(width), Flt(depth))),
        Arr(Str('flag_layout'), Arr())))))
    out.append(('VrTitle', text_field((LEFT, TOP - 56, 0), (0, 0, 700, 40), (), MENU_LABEL[language], 30)))
    # A row's button: the chooser's, lower and closer together so that all rows fit; each goes
    # under the one before it (layout flag 8, as the game's own buttons do).
    base = src['Button02Base']
    assert base.kind == 'arr' and base.value[0] == Str('Button') and base.value[4] == Arr(Int(8))
    row = Arr(*base.value)
    row.value[2] = Arr(Flt(0.0), Flt(ROW_GAP), Flt(0.0))
    row.value[3] = Arr(Flt(0.0), Flt(0.0), Flt(NAME_WIDTH), Flt(ROW_HEIGHT))
    out.append(('VrRowBase', row))
    # Its value: a text field of the same pitch in a column of its own beside the buttons.
    out.append(('VrValBase', Arr(
        Str('TextField'), Str('app:/UI/Transparent_skin.sgo'),
        Arr(Flt(0.0), Flt(ROW_GAP + 4), Flt(0.0)), Arr(Flt(0.0), Flt(0.0), Flt(VALUE_WIDTH), Flt(ROW_HEIGHT - 4)),
        Arr(Int(8)), Int(0), Int(0),
        Arr(Arr(Str('font_size'), Arr(Int(24), Int(24))), Arr(Str('font_border_width'), Flt(2.5))))))
    buttons, fields = [], []
    for i, names in enumerate(ROWS):
        out.append((button_name(i), Arr(Arr(Int(0), Str('VrRowBase')), Arr(
            Arr(Str('text_dr'), Str(names[language])),
            Arr(Str('callback'), Arr(Str('cbVrSet'), Arr(Arr(Str('item'), Int(i)))))))))
        out.append((value_name(i), Arr(Arr(Int(0), Str('VrValBase')), Arr(Arr(Str('text_dr'), Str('-'))))))
        buttons.extend([Arr(Str(button_name(i))), Int(0)])
        fields.extend([Arr(Str(value_name(i))), Int(0)])
    out.append(('VrNote', Arr(Arr(Int(0), Str('VrRowBase')), Arr(
        Arr(Str('text_dr'), Str(NOTE[language])),
        Arr(Str('callback'), Arr(Str('cbVrSet'), Arr(Arr(Str('item'), Int(-1)))))))))
    buttons.extend([Arr(Str('VrNote')), Int(0)])
    column = Arr(*src['LeftButtonsBase'].value)     # the chooser's own column of buttons
    column.value[2] = Arr(Flt(LEFT), Flt(TOP), Flt(0.0))
    column.value[3] = Arr(Flt(0.0), Flt(0.0), Flt(NAME_WIDTH), Flt(column_height))
    out.append(('VrColumn', column))
    values = Arr(*src['LeftButtonsBase'].value)
    values.value[2] = Arr(Flt(LEFT + NAME_WIDTH + BETWEEN), Flt(TOP), Flt(0.0))
    values.value[3] = Arr(Flt(0.0), Flt(0.0), Flt(VALUE_WIDTH), Flt(height))
    out.append(('VrValues', values))
    out.append(('focus_start', Str(button_name(0))))
    out.append(('layout_tree', Arr(Arr(Str('BaseFrame')), Arr(
        Arr(Str('VrWindow')), Int(0), Arr(Str('VrTitle')), Int(0),
        Arr(Str('VrColumn')), Arr(*buttons), Arr(Str('VrValues')), Arr(*fields)))))
    return out


def main():
    out_dir = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / 'assets' / 'ui'
    archive = cpk.Cpk(os.path.join(GAME, 'Root.cpk'))
    pause = game_file(archive, 'LYT_PAUSEMENU.SGO')
    select = game_file(archive, 'LYT_ONOFFSELECT.SGO')
    checked(pause, 'LYT_PAUSEMENU.SGO')
    checked(select, 'LYT_ONOFFSELECT.SGO')
    out_dir.mkdir(parents=True, exist_ok=True)
    for language, suffix in ((0, ''), (1, '.JA')):
        for name, values in (('LYT_PAUSEMENU%s.SGO' % suffix, pause_menu(pause, language)),
                             ('LYT_EDF6VR_SETTING%s.SGO' % suffix, setting_window(select, language))):
            data = sgo_write.write(values)
            assert dict(sgo_write.parse(data)) == dict(values), name
            (out_dir / name).write_bytes(data)
            print(f'wrote {out_dir / name} ({len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()})')
    inc = HERE.parent / 'src' / 'vr_menu_rows.inc'
    text = '// Written by tools/make_vr_menu.py: the rows of the layouts in assets/ui.\n#define EDF6VR_MENU_ROWS %d\n' % len(ROWS)
    if not inc.exists() or inc.read_text(encoding='utf-8') != text:
        inc.write_text(text, encoding='utf-8', newline='\n')
    print(len(ROWS), 'rows')


if __name__ == '__main__':
    main()
