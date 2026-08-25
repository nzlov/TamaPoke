# Question-bank sources

Each `*.json` file in this directory is compiled into an indexed `.tquiz` pack
by `python3 tools/gen_data_packs.py`. The same JSON can be imported or exported
by the Web question-bank builder.

```json
{
  "schema": 1,
  "id": "math-basics-zh",
  "label": "基础常识",
  "revision": 1,
  "questions": [
    {
      "id": "q001",
      "locale": "zh-CN",
      "stem": "下面哪个数是偶数？",
      "options": ["1", "2", "3", "5"],
      "answer": 1
    }
  ]
}
```

`answer` is the zero-based index of the correct option. IDs must be stable
within a locale so an edited pack can retain question identity.
