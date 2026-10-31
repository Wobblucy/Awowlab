# Contributing to AwowLab

Thanks for wanting to help.

## Licence

AwowLab is free software under the GNU Affero General Public License v3.0
(`LICENSE`). You can use, study, change and share it. If you distribute a
modified version, or let people use one over a network, you must publish its
full source under the same licence.

If you want to build AwowLab into something you do not intend to open-source,
a commercial licence is available from the maintainer. Open an issue on the
repository to ask.

## Contributor agreement

Pull requests are accepted only from contributors who agree to the
[Contributor License Agreement](CLA.md). It lets the maintainer keep offering
the commercial licence alongside the AGPL; you keep the rights to your own
work. Agreeing is one line in your first pull request; see the end of `CLA.md`.

## Making a change

- Branch from `dev` and open the pull request against `dev`. `master` only
  moves when a release is made.
- Keep commits focused. Subjects are short and imperative:
  `Add ...`, `Fix ...`, `Update ...`, `Refactor ...`.
- The build treats warnings as errors. Build and run the tests before opening
  a pull request.
- User-facing text goes through the language files in `AwowLab/data/lang/`,
  never as literal strings in the UI code. Add new keys to `en_US.csv`; other
  languages fall back to English until they are translated.

## Translations

Each language is one file in `AwowLab/data/lang/`, named by its locale
(`de_DE.csv`, `fr_FR.csv` and so on). Every line is `key,value`; lines that
start with `#` are comments. To improve a translation, edit the file for your
language and open a pull request.

- Change only the text after the first comma. Keys stay exactly as they are,
  and new keys are added to `en_US.csv` first.
- Keep every `%s`, `%d`, `%.1f` and similar placeholder, in the same order as
  the English text. A line whose placeholders differ from English is ignored
  and the English text is shown instead.
- Wrap a value in double quotes if it starts with one; write a literal quote
  inside a quoted value as `""`.
- Save the file as UTF-8.
