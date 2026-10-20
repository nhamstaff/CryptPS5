import json

d = json.load(open('build_progress/progress.json'))
groups = d['libraries']['groups']
total_todo = sum(g['todo'] for g in groups)
print(f"Total todo: {total_todo}")
for g in sorted(groups, key=lambda x: x['todo'], reverse=True):
    if g['todo'] > 0:
        print(f"{g['name']} ({g['todo']}): {', '.join(g['todo_names'])}")
