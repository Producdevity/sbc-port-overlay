# SPDX-License-Identifier: MIT
"""Remove only the owned verification entry, preserving all other bytes."""
from pathlib import Path, PurePosixPath
import os
import stat
import sys
import tempfile
from xml.etree import ElementTree
from xml.parsers import expat


def remove_entry(data: bytes, target: str) -> bytes:
    name = PurePosixPath(target).name
    if not name.startswith("SPO Verify-") or not name.endswith(".sh"):
        raise ValueError("not a verification launcher")
    accepted = {target, "./" + name, name}
    parser = expat.ParserCreate()
    parser.SetParamEntityParsing(expat.XML_PARAM_ENTITY_PARSING_NEVER)
    parser.ExternalEntityRefHandler = lambda *args: 0
    depth = 0
    current = None
    in_path = False
    spans = []

    def start(tag, attrs):
        nonlocal depth, current, in_path
        if depth == 0 and tag != "gameList":
            raise ValueError("not an EmulationStation gamelist")
        if depth == 1 and tag == "game":
            current = [parser.CurrentByteIndex, []]
        if depth == 2 and tag == "path" and current is not None:
            in_path = True
        depth += 1

    def content(text):
        if in_path and current is not None:
            current[1].append(text)

    def end(tag):
        nonlocal depth, current, in_path
        depth -= 1
        if depth == 2 and tag == "path":
            in_path = False
        if depth == 1 and tag == "game" and current is not None:
            if "".join(current[1]).strip() in accepted:
                begin = current[0]
                finish = data.index(b">", parser.CurrentByteIndex) + 1
                line_begin = data.rfind(b"\n", 0, begin) + 1
                if not data[line_begin:begin].strip(b" \t"):
                    begin = line_begin
                line_end = data.find(b"\n", finish)
                if line_end >= 0 and not data[finish:line_end].strip(b" \t\r"):
                    finish = line_end + 1
                spans.append((begin, finish))
            current = None

    parser.StartElementHandler = start
    parser.CharacterDataHandler = content
    parser.EndElementHandler = end
    parser.Parse(data, True)
    result = bytearray()
    position = 0
    for begin, finish in spans:
        result.extend(data[position:begin])
        position = finish
    result.extend(data[position:])
    return bytes(result)


def empty_gamelist(data: bytes) -> bool:
    root = ElementTree.fromstring(data, parser=ElementTree.XMLParser(target=ElementTree.TreeBuilder(insert_comments=True)))
    return root.tag == 'gameList' and not root.attrib and len(root) == 0 and not (root.text or '').strip()


if __name__ == "__main__":
    path = Path(sys.argv[1])
    before = path.read_bytes()
    after = remove_entry(before, sys.argv[2])
    if sys.argv[3:] == ['--remove-empty'] and empty_gamelist(after):
        path.unlink()
        sys.exit(0)
    if before != after:
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".spo-gamelist-", delete=False) as out:
                temporary = Path(out.name)
                os.chmod(temporary, stat.S_IMODE(path.stat().st_mode))
                out.write(after)
                out.flush()
                os.fsync(out.fileno())
            os.replace(temporary, path)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
