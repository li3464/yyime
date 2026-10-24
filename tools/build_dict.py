"""把 Rime 格式的词库编译成 yy输入法 用的二进制词库。

用法: python -I build_dict.py <data_src目录> <输出目录> <报告文件>

输出:
  pinyin.bin   拼音词库（按音节序列建的前缀树）
  wubi86.bin   五笔码表（按编码排序）
  english.bin  英文单词表（按小写排序）
  emoji.txt    词后面跟的表情
  chaizi.txt   生僻字拆字表、stroke.txt 笔画表（u 模式用）

所有整数都是小端序，文字统一存成 UTF-16LE，方便 C++ 那边直接用。
"""
import math
import os
import re
import struct
import sys
from collections import defaultdict, deque

# 声母（可空）+ 以元音开头的韵母；另外几个叹词音节单独放行
SYL_RE = re.compile(r'^(?:zh|ch|sh|[bpmfdtnlgkhjqxrzcsyw])?[aeiouv][a-z]{0,4}$|^(?:m|n|ng|hm|hng)$')
SPELLING_FIX = {'lue': 'lve', 'nue': 'nve'}
COST_SCALE = 1000.0  # 代价 = -ln(概率) * 1000，存成 uint16


def rime_rows(path):
    """逐行产出 Rime 词库正文（'...' 之后）里的字段，跳过注释和空行。"""
    in_body = False
    with open(path, encoding='utf-8') as f:
        for raw in f:
            line = raw.rstrip('\r\n')
            if not in_body:
                in_body = line.strip() == '...'
                continue
            if line and not line.startswith('#'):
                yield line.split('\t')


def parse_weight(s):
    s = s.strip()
    if not s or s.endswith('%'):
        return None
    try:
        return float(s)
    except ValueError:
        return None


def is_han(ch):
    cp = ord(ch)
    return (0x3400 <= cp <= 0x9FFF or 0xF900 <= cp <= 0xFAFF or 0x20000 <= cp <= 0x3FFFF
            or ch == '〇')


RARE_COST = 60000  # 大字表补进来的生僻字的代价，比所有常用词都大；C++ 的 kRareCost 要跟着改


def font_ok(ch):
    """生僻字只收系统字体显示得出来的区：基本区、扩展 A、兼容区、扩展 B（更后面的扩展区多半是方块）"""
    cp = ord(ch)
    return (0x3400 <= cp <= 0x4DBF or 0x4E00 <= cp <= 0x9FFF or 0xF900 <= cp <= 0xFAFF
            or 0x20000 <= cp <= 0x2A6DF or ch == '〇')


def to_cost(weight, total):
    c = -math.log(max(weight, 1.0) / total) * COST_SCALE
    return max(0, min(65535, int(round(c))))


def utf16_units(s):
    return len(s.encode('utf-16-le')) // 2


class TextPool:
    """去重的 UTF-16 字符串池，偏移量以 UTF-16 码元计。"""

    def __init__(self):
        self.parts = []
        self.size = 0
        self.index = {}

    def add(self, s):
        off = self.index.get(s)
        if off is None:
            off = self.size
            self.index[s] = off
            self.parts.append(s)
            self.size += utf16_units(s)
        return off

    def blob(self):
        return ''.join(self.parts).encode('utf-16-le')


def pad4(b):
    return b + b'\0' * (-len(b) % 4)


# ---------------------------------------------------------------- 拼音

PINYIN_SOURCES = [
    # (文件, 没写词频时给的默认词频)
    ('rime-ice/cn_dicts_8105.dict.yaml', 1.0),
    ('rime-ice/cn_dicts_base.dict.yaml', 1.0),
    ('rime-ice/cn_dicts_ext.dict.yaml', 1.0),
    ('rime-ice/cn_dicts_others.dict.yaml', 1.0),  # 容错读音，放最后面
]


def load_pinyin_entries(src, log):
    merged = {}  # (词, 音节元组) -> 词频
    skipped = defaultdict(int)
    syl_seen = defaultdict(int)
    for rel, default_w in PINYIN_SOURCES:
        n = 0
        for row in rime_rows(os.path.join(src, rel)):
            if len(row) < 2:
                skipped['列数不够'] += 1
                continue
            text = row[0].strip()
            syls = [SPELLING_FIX.get(s, s) for s in row[1].strip().lower().split()]
            if not text or not syls:
                skipped['空字段'] += 1
                continue
            if not all(is_han(ch) for ch in text):
                skipped['含非汉字'] += 1
                continue
            if len(text) != len(syls):
                skipped['字数和音节数不符'] += 1
                continue
            if not all(SYL_RE.match(s) for s in syls):
                skipped['音节不合法'] += 1
                continue
            w = parse_weight(row[2]) if len(row) > 2 else None
            if w is None:
                w = default_w
            key = (text, tuple(syls))
            if merged.get(key, -1.0) < w:
                merged[key] = w
            for s in syls:
                syl_seen[s] += 1
            n += 1
        log(f'{rel}: 收了 {n} 条')
    for k, v in sorted(skipped.items()):
        log(f'跳过（{k}）: {v}')
    return merged, syl_seen


def build_trie(keys):
    """keys: 排好序的音节 id 元组列表。按层（BFS）生成节点，让每个节点的孩子连续存放。
    返回 nodes: [syl, first_child, child_count, key_index]"""
    nodes = [[0, 0, 0, -1]]
    queue = deque([(0, 0, len(keys), 0)])
    while queue:
        ni, lo, hi, depth = queue.popleft()
        i = lo
        if i < hi and len(keys[i]) == depth:
            nodes[ni][3] = i
            i += 1
        first = len(nodes)
        count = 0
        while i < hi:
            s = keys[i][depth]
            j = i + 1
            while j < hi and keys[j][depth] == s:
                j += 1
            nodes.append([s, 0, 0, -1])
            queue.append((len(nodes) - 1, i, j, depth + 1))
            count += 1
            i = j
        nodes[ni][1] = first
        nodes[ni][2] = count
    return nodes


def build_pinyin(src, out_path, log):
    merged, syl_seen = load_pinyin_entries(src, log)
    syllables = sorted(syl_seen)
    syl_id = {s: i for i, s in enumerate(syllables)}
    log(f'音节 {len(syllables)} 个: ' + ' '.join(syllables))
    rare = [f'{s}({syl_seen[s]})' for s in syllables if syl_seen[s] < 5]
    log('很少用到的音节: ' + ' '.join(rare))

    total = sum(merged.values())
    by_key = defaultdict(list)
    char_cost = {}  # 单字 -> 最常用读音的代价，生僻字排序用
    for (text, syls), w in merged.items():
        cost = to_cost(w, total)
        by_key[tuple(syl_id[s] for s in syls)].append((cost, text))
        if len(text) == 1:
            char_cost[text] = min(cost, char_cost.get(text, 65535))
    max_cost = max(c for items in by_key.values() for c, _ in items)
    if max_cost >= RARE_COST:
        raise SystemExit(f'常用词的代价 {max_cost} 碰到了生僻字的 {RARE_COST}，要调大 RARE_COST')

    # 大字表里的生僻字补进来，代价固定排在所有常用字后面；音节表不变，认不出的读音不收
    readings = defaultdict(list)  # 字 -> 不带声调的读音（拆字、笔画的读音备注兜底用）
    for (text, syls) in merged:
        if len(text) == 1 and syls[0] not in readings[text]:
            readings[text].append(syls[0])
    added = 0
    rare_path = os.path.join(src, 'rime-ice/cn_dicts_41448.dict.yaml')
    for row in rime_rows(rare_path) if os.path.exists(rare_path) else []:
        if len(row) < 2 or len(row[0]) != 1 or not font_ok(row[0]):
            continue
        ch, syl = row[0], SPELLING_FIX.get(row[1].strip().lower(), row[1].strip().lower())
        if syl not in syl_id:
            continue
        if syl not in readings[ch]:
            readings[ch].append(syl)
        if (ch, (syl,)) in merged:
            continue
        merged[(ch, (syl,))] = 0.0
        by_key[(syl_id[syl],)].append((RARE_COST + (0 if ord(ch) < 0x10000 else 1000), ch))
        added += 1
    log(f'生僻字：从大字表补进 {added} 条（代价 {RARE_COST} 起），常用词最大代价 {max_cost}')
    keys = sorted(by_key)
    nodes = build_trie(keys)

    pool = TextPool()
    entry_blob = bytearray()
    node_blob = bytearray()
    entry_count = 0
    for syl, first_child, child_count, ki in nodes:
        first_entry = entry_count
        n = 0
        if ki >= 0:
            for cost, text in sorted(by_key[keys[ki]]):
                entry_blob += struct.pack('<IHH', pool.add(text), utf16_units(text), cost)
                n += 1
        entry_count += n
        node_blob += struct.pack('<IIIHH', first_child, first_entry, n, syl, child_count)

    syl_blob = b''.join(s.encode('ascii').ljust(8, b'\0') for s in syllables)
    text_blob = pool.blob()
    header_size = 64
    off_syl = header_size
    off_nodes = off_syl + len(syl_blob)
    off_entries = off_nodes + len(node_blob)
    off_text = off_entries + len(entry_blob)
    header = struct.pack('<8sIIIIIIII', b'YYPYDIC1', len(syllables), len(nodes), entry_count,
                         pool.size, off_syl, off_nodes, off_entries, off_text)
    with open(out_path, 'wb') as f:
        f.write(header.ljust(header_size, b'\0'))
        f.write(syl_blob)
        f.write(node_blob)
        f.write(entry_blob)
        f.write(text_blob)
    log(f'pinyin.bin: 词条 {entry_count}，节点 {len(nodes)}，文字 {pool.size} 码元，'
        f'{os.path.getsize(out_path) / 1048576:.1f} MB')

    # 抽查几个读音的前几名，看词频合不合理
    for probe in ['de', 'shi', 'ni hao', 'zhong guo', 'xi an', 'xian', 'fang an', 'jin tian']:
        ids = tuple(syl_id.get(s, -1) for s in probe.split())
        items = sorted(by_key.get(ids, []))[:8]
        log(f'  [{probe}] ' + ' '.join(f'{t}:{c}' for c, t in items))
    chars = sum(w for (t, _), w in merged.items() if len(t) == 1)
    log(f'单字词频占比 {chars / total:.3f}，总词频 {total:.3g}')
    return char_cost, readings


# ---------------------------------------------------------------- 五笔

def build_wubi(src, out_path, log):
    merged = {}
    skipped = 0
    for row in rime_rows(os.path.join(src, 'rime-wubi/wubi86.dict.yaml')):
        if len(row) < 2:
            skipped += 1
            continue
        text, code = row[0].strip(), row[1].strip().lower()
        if not text or not code.isascii() or not code.isalpha() or len(code) > 4:
            skipped += 1
            continue
        w = (parse_weight(row[2]) if len(row) > 2 else None) or 1.0
        key = (code, text)
        if merged.get(key, -1.0) < w:
            merged[key] = w
    total = sum(merged.values())
    rows = sorted((code, to_cost(w, total), text) for (code, text), w in merged.items())
    pool = TextPool()
    entry_blob = bytearray()
    for code, cost, text in rows:
        entry_blob += struct.pack('<4sIHH', code.encode('ascii'), pool.add(text), utf16_units(text), cost)
    write_flat(out_path, b'YYWBDIC1', len(rows), pool, entry_blob)
    log(f'wubi86.bin: {len(rows)} 条（跳过 {skipped}），{os.path.getsize(out_path) / 1048576:.1f} MB')
    for probe in ['a', 'g', 'aa', 'wq', 'trnt', 'tmlk']:
        log(f'  [{probe}] ' + ' '.join(f'{t}:{c}' for code, c, t in rows if code == probe))


# ---------------------------------------------------------------- 英文

def build_english(src, out_path, log):
    words = set()
    for rel in ['rime-ice/en_dicts_en.dict.yaml', 'rime-ice/en_dicts_en_ext.dict.yaml']:
        for row in rime_rows(os.path.join(src, rel)):
            w = row[0].strip()
            if 2 <= len(w) <= 30 and w.isascii() and w.isalpha():
                words.add(w)
    rows = sorted(words, key=lambda w: (w.lower(), w))
    pool = TextPool()
    entry_blob = bytearray()
    for w in rows:
        flags = 1 if any(c.isupper() for c in w) else 0
        entry_blob += struct.pack('<IHH', pool.add(w), len(w), flags)
    write_flat(out_path, b'YYENDIC1', len(rows), pool, entry_blob)
    log(f'english.bin: {len(rows)} 个单词，{os.path.getsize(out_path) / 1024:.0f} KB')


def write_flat(out_path, magic, count, pool, entry_blob):
    header_size = 64
    off_entries = header_size
    off_text = off_entries + len(pad4(bytes(entry_blob)))
    header = struct.pack('<8sIIII', magic, count, pool.size, off_entries, off_text)
    with open(out_path, 'wb') as f:
        f.write(header.ljust(header_size, b'\0'))
        f.write(pad4(bytes(entry_blob)))
        f.write(pool.blob())


def build_emoji(src, out_path, log):
    """rime-ice 的 opencc/emoji.txt（词<TAB>词 表情…）整理成 词<TAB>表情 表情…"""
    path = os.path.join(src, 'rime-ice/opencc_emoji.txt')
    if not os.path.exists(path):
        log('没有 emoji 数据，跳过')
        return
    emojis = {}
    with open(path, encoding='utf-8') as f:
        for raw in f:
            parts = raw.rstrip('\r\n').split('\t')
            if len(parts) != 2 or not parts[0]:
                continue
            word = parts[0]
            items = emojis.setdefault(word, [])
            for e in parts[1].split(' '):
                if e and e != word and e not in items:
                    items.append(e)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        for word, items in emojis.items():
            if items:
                f.write(word + '\t' + ' '.join(items[:6]) + '\n')
    log(f'emoji.txt: {len(emojis)} 个词')


# ---------------------------------------------------------------- 生僻字（u 模式）

def load_tone_readings(src):
    """pinyin-data 的 pinyin.txt（U+7131: yàn,yì  # 焱）→ 字 -> 'yàn/yì'"""
    path = os.path.join(src, 'pinyin-data/pinyin.txt')
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding='utf-8') as f:
        for raw in f:
            line = raw.split('#', 1)[0].strip()
            if not line.startswith('U+') or ':' not in line:
                continue
            cp, values = line.split(':', 1)
            items = [v.strip() for v in values.split(',') if v.strip()]
            if items:
                out[chr(int(cp[2:], 16))] = '/'.join(items[:3])
    return out


def build_rare(src, out_dir, char_cost, readings, log):
    """拆字表和笔画表：每行 编码<TAB>字<TAB>排名<TAB>读音，按编码排好，C++ 那边二分找前缀。
    排名越小越常用：常用字用拼音词库里的代价，生僻字排在后面（拆字表自带的字频再细分一下）。"""
    tones = load_tone_readings(src)

    def reading(ch):
        return tones.get(ch) or '/'.join(readings.get(ch, [])[:3])

    radical_rows = []
    radical_weight = {}
    path = os.path.join(src, 'rime-ice/radical_pinyin.dict.yaml')
    for row in rime_rows(path) if os.path.exists(path) else []:
        if len(row) < 2 or len(row[0]) != 1:
            continue
        code = row[1].strip().lower().replace("'", '').replace(' ', '')
        if code.isascii() and code.isalpha():
            radical_rows.append((code, row[0]))
            w = parse_weight(row[2]) if len(row) > 2 else None
            radical_weight[row[0]] = max(radical_weight.get(row[0], 0), int(w or 0))
    stroke_rows = []
    path = os.path.join(src, 'rime-stroke/stroke.dict.yaml')
    for row in rime_rows(path) if os.path.exists(path) else []:
        if len(row) >= 2 and len(row[0]) == 1 and row[1] and set(row[1]) <= set('hspnz'):
            stroke_rows.append((row[1], row[0]))

    def rank(ch):
        if ch in char_cost:
            return char_cost[ch]
        return RARE_COST + (0 if ord(ch) < 0x10000 else 2000) - min(radical_weight.get(ch, 0), 999)

    for name, rows in (('chaizi.txt', radical_rows), ('stroke.txt', stroke_rows)):
        lines = set()
        for code, ch in rows:
            r = reading(ch)
            if font_ok(ch) and r:  # 读不出来、显示不了的字不收
                lines.add((code, rank(ch), ch, r))
        with open(os.path.join(out_dir, name), 'w', encoding='utf-8', newline='\n') as f:
            for code, rk, ch, r in sorted(lines):
                f.write(f'{code}\t{ch}\t{rk}\t{r}\n')
        log(f'{name}: {len(lines)} 行，{len({ch for _, _, ch, _ in lines})} 个字')


def main():
    src, out_dir, report = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(out_dir, exist_ok=True)
    lines = []

    def log(msg):
        lines.append(msg)
        with open(report, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines) + '\n')

    char_cost, readings = build_pinyin(src, os.path.join(out_dir, 'pinyin.bin'), log)
    build_rare(src, out_dir, char_cost, readings, log)
    build_wubi(src, os.path.join(out_dir, 'wubi86.bin'), log)
    build_english(src, os.path.join(out_dir, 'english.bin'), log)
    build_emoji(src, os.path.join(out_dir, 'emoji.txt'), log)
    log('完成')


if __name__ == '__main__':
    main()
