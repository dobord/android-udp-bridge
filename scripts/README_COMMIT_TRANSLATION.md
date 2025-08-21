Workflow: translate git commit messages to English

1) Export current commit messages:
   ./scripts/export_commit_messages.sh
   - Creates `translations.txt` with blocks for each commit. Edit the message blocks and replace with the English translations. Keep the markers (===COMMIT=== <sha> and ===END===).

2) Apply translations (rewrites history):
   ./scripts/apply_translated_messages.sh
   - Creates a backup branch `backup/<branch>` and a copy `<branch>-old`, then runs git filter-branch to replace messages using `scripts/msg_replace.py`.

Warnings and notes:
- This rewrites history. Coordinate with collaborators and be prepared to force-push.
- Review `translations.txt` carefully. The script will replace messages only for commits whose sha appears in the file.
- You can automate translations separately, but keep the translated messages accurate and in English.
