#!/usr/bin/env python3
"""Validate the execution tracker and render its human-readable completion plan."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TRACKER = ROOT / 'docs/completion-progress.json'
PLAN = ROOT / 'docs/completion-plan.md'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(plan):
    require(plan['schema_version'] == 1, 'Unsupported tracker schema')
    milestones = plan['milestones']
    ids = [m['id'] for m in milestones]
    require(ids == [f'M{i:02}' for i in range(33)], 'Keep the fixed M00–M32 sequence')
    phases = {p['id'] for p in plan['phases']}
    require(len(phases) == len(plan['phases']), 'Duplicate phase IDs')
    requirements = {r['id']: r for r in plan['requirements']}
    require(len(requirements) == len(plan['requirements']), 'Duplicate requirement IDs')
    require(requirements, 'Requirements must not be empty')
    for requirement in requirements.values():
        owners = requirement['milestones']
        require(owners and len(owners) == len(set(owners)), f"Invalid owners: {requirement['id']}")
        require(set(owners) <= set(ids), f"Unknown requirement owner: {requirement['id']}")
    all_tasks = set()
    unfinished = []
    for index, milestone in enumerate(milestones):
        mid = milestone['id']
        require(milestone['phase'] in phases, f'Unknown phase: {mid}')
        require(milestone['depends_on'] == ids[max(0, index - 1):index], f'Invalid predecessor: {mid}')
        require(milestone['status'] in {'pending', 'ready', 'in_progress', 'blocked', 'complete'}, f'Invalid status: {mid}')
        require(milestone['tasks'] and milestone['acceptance'], f'Missing tasks or acceptance: {mid}')
        require(set(milestone['requirements']) <= set(requirements), f'Unknown requirement: {mid}')
        for rid in milestone['requirements']:
            require(mid in requirements[rid]['milestones'], f'Asymmetric requirement link: {mid}/{rid}')
        for task_index, task in enumerate(milestone['tasks'], 1):
            tid = task['id']
            require(tid == f'{mid}.{task_index}' and tid not in all_tasks, f'Invalid task ID: {tid}')
            all_tasks.add(tid)
            require(type(task['done']) is bool, f'Task done must be boolean: {tid}')
            require(not task['done'] or task.get('evidence'), f'Completed task needs evidence: {tid}')
        for source in milestone['sources']:
            require((ROOT / source).is_file(), f'Missing source reference: {source}')
        if milestone['status'] == 'complete':
            require(all(t['done'] for t in milestone['tasks']), f'Incomplete tasks: {mid}')
            require(milestone['evidence'] and milestone.get('completed_on'), f'Missing completion evidence/date: {mid}')
            acceptance = milestone.get('acceptance_evidence', [])
            require(len(acceptance) == len(milestone['acceptance']) and all(acceptance), f'Record evidence for every acceptance criterion: {mid}')
            require(all(m['status'] == 'complete' for m in milestones[:index]), f'Unfinished predecessor: {mid}')
        else:
            unfinished.append(milestone)
        if milestone['status'] == 'blocked':
            require(milestone['blocker'], f'Missing blocker explanation: {mid}')
        else:
            require(milestone['blocker'] is None, f'Clear resolved blocker: {mid}')
    for rid, requirement in requirements.items():
        require(all(rid in milestones[ids.index(mid)]['requirements'] for mid in requirement['milestones']), f'Asymmetric owners: {rid}')
    current = plan['current']
    if unfinished:
        first = unfinished[0]
        require(current and current['milestone'] == first['id'], 'Current pointer must select first unfinished milestone')
        pending = [t for t in first['tasks'] if not t['done']]
        require(current['task'] == (pending[0]['id'] if pending else None), 'Current task must be first unfinished task; null means acceptance review')
        require(current['next_action'].strip(), 'Current task needs a concrete next action')
        require(first['status'] in {'ready', 'in_progress', 'blocked'}, 'Current milestone must be ready, in progress or blocked')
        require(all(m['status'] == 'pending' and not any(t['done'] for t in m['tasks']) for m in unfinished[1:]), 'Later work must remain pending until its predecessor finishes')
    else:
        require(current is None, 'A completed release must clear the current pointer')
        require(not any(d['severity'] in {'P0', 'P1'} for d in plan['open_defects']), 'Release cannot retain P0/P1 defects')


def render(plan):
    lines = []
    def add(*values):
        lines.extend(values)
    def bullets(values):
        for value in values:
            add(f'- {value}')
        add('')
    def link(source):
        relative = source[5:] if source.startswith('docs/') else '../' + source
        return f'[{source}]({relative})'
    add(f"# {plan['title']}", '', '<!-- Generated by tools/render_completion_plan.py. Edit completion-progress.json, then regenerate. -->', '', plan['purpose'], '', f"Updated: **{plan['updated']}** · Plan revision: **{plan['plan_revision']}**", '')
    add('## Current work', '')
    current = plan['current']
    if current:
        add(f"**{current['task'] or current['milestone'] + ' acceptance review'}** — {current['next_action']}", '')
    else:
        add('All release milestones are complete; consult the recorded release evidence.', '')
    completed = sum(m['status'] == 'complete' for m in plan['milestones'])
    tasks = sum(len(m['tasks']) for m in plan['milestones'])
    add(f"This register contains **{len(plan['milestones'])} milestones / {tasks} tasks** for remaining work. **{completed} milestones have passed their gates.** This is not a percentage of the whole game: the working baseline below already exists.", '', 'The JSON [progress tracker](completion-progress.json) is authoritative. To update this document, edit that tracker and run `python3 tools/render_completion_plan.py` from the project root; use `--check` to check for drift. Root [AGENTS.md](../AGENTS.md) tells implementation sessions to resume this pointer.', '')
    baseline = plan['baseline']
    add('## Retained baseline and scope', '', f"Baseline: `{baseline['commit']}` ({baseline['recorded']}). {baseline['verification']}", '')
    bullets(baseline['retain'])
    add(f"Accepted design: {link(plan['authoritative_requirements'])}. Historical implementation evidence: {link(plan['historical_status'])}. Older status entries do not select the next task.", '', f"Platform target: {plan['platform_target']}", '', '**Cancelled scope; do not reintroduce:**', '')
    bullets(plan['cancelled_scope'])
    add('## Fixed execution order', '', '| Phase | Milestones | Exit goal |', '| --- | --- | --- |')
    for phase in plan['phases']:
        members = [m for m in plan['milestones'] if m['phase'] == phase['id']]
        span = members[0]['id'] + ('–' + members[-1]['id'] if len(members) > 1 else '')
        add(f"| {phase['id']}: {phase['title']} | [{span}](#{members[0]['id'].lower()}) | {phase['exit_goal']} |")
    add('', 'Gameplay comes first. Performance work is P7; an earlier correctness or responsiveness fix stays attached to the milestone it unblocks.', '', '## Rules for continuing work', '')
    bullets(plan['execution_policy'])
    add('## Completion criteria for every milestone', '')
    bullets(plan['definition_of_done'])
    add('Every completed task needs evidence in its `evidence` list. A completed milestone also needs a date, an overall evidence record and one `acceptance_evidence` entry per criterion below. When tasks are finished but acceptance is under review, keep the milestone active with a null current task. Advance the pointer only after the gate passes. M00 is a one-time inventory, not a recurring restart.', '')
    add('## Ordered work', '')
    for milestone in plan['milestones']:
        mid = milestone['id']
        add(f'<a id="{mid.lower()}"></a>', '', f"### {mid} — {milestone['title']}", '', f"**Status:** {milestone['status']} · **Depends on:** {', '.join(milestone['depends_on']) or 'retained baseline'} · **Phase:** {milestone['phase']}", '', '**Tasks**', '')
        for task in milestone['tasks']:
            add(f"- [{'x' if task['done'] else ' '}] **{task['id']}** {task['title']}")
            for evidence in task.get('evidence', []):
                add(f'  - Evidence: {evidence}')
        add('', '**Gate — all must pass**', '')
        for index, criterion in enumerate(milestone['acceptance'], 1):
            add(f'{index}. {criterion}')
        add('', '**Requirement coverage:** ' + ', '.join(milestone['requirements']), '', '**Starting references:** ' + ', '.join(link(s) for s in milestone['sources']), '')
        if milestone['evidence']:
            add('**Recorded milestone evidence:**', '')
            bullets(milestone['evidence'])
        for index, evidence in enumerate(milestone.get('acceptance_evidence', []), 1):
            add(f'Gate {index} evidence: {evidence}', '')
        if milestone['blocker']:
            add(f"**Blocker:** {milestone['blocker']}", '')
    add('## Requirement coverage register', '', 'Baseline classifications describe existing foundations; they are not completion claims. M00 checks the native code and preserved browser snapshots and records any accepted parity detail under its existing owner milestone.', '', '| ID | Accepted requirement | Baseline | Owner milestones |', '| --- | --- | --- | --- |')
    for requirement in plan['requirements']:
        owners = ', '.join(f'[{mid}](#{mid.lower()})' for mid in requirement['milestones'])
        add(f"| {requirement['id']} | {requirement['title']} | {requirement['baseline']} | {owners} |")
    add('', '## Required whole-game release scenarios', '')
    bullets(plan['release_scenarios'])
    add('## Open defects', '')
    if plan['open_defects']:
        for defect in plan['open_defects']:
            add(f"- **{defect['id']} ({defect['severity']})** {defect['description']} — owner {defect['milestone']}")
        add('')
    else:
        add('No defects have been entered in this planning-only register yet. This is not a defect-free claim; M00 records the reproduced baseline defects.', '')
    add('## Future ideas outside the release gate', '')
    if plan['future_ideas']:
        bullets(plan['future_ideas'])
    else:
        add('None recorded. Optional new ideas go here rather than silently expanding release scope.', '')
    add('## Plan changes', '')
    for change in plan['change_log']:
        add(f"- **{change['date']}** — {change['change']}")
    return '\n'.join(lines).rstrip() + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Validate without writing; fail if Markdown is stale')
    args = parser.parse_args()
    try:
        plan = json.loads(TRACKER.read_text(encoding='utf-8'))
        validate(plan)
        output = render(plan)
        if args.check:
            require(PLAN.is_file() and PLAN.read_text(encoding='utf-8') == output, 'completion-plan.md is stale; regenerate it')
            print('Completion tracker and generated plan are consistent.')
        else:
            PLAN.write_text(output, encoding='utf-8')
            print(f'Wrote {PLAN.relative_to(ROOT)}')
    except (ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'Invalid completion tracker: {error}\n')


if __name__ == '__main__':
    main()
