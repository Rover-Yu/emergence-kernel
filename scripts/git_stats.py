#!/usr/bin/env python3
"""
Git Statistics Analyzer

Analyzes code changes on the main branch by day, week, and month.
"""

import argparse
import subprocess
import sys
from collections import defaultdict
from datetime import datetime, timedelta


def run_git_command(cmd):
    """Run a git command and return output."""
    result = subprocess.run(
        cmd,
        shell=True,
        capture_output=True,
        text=True,
        check=True
    )
    return result.stdout.strip()


def get_commit_stats(since="--1-year"):
    """Get commit statistics from git log."""
    cmd = [
        "git", "log", f"--since={since}", "--pretty=format:%H|%ai|%an",
        "--numstat"
    ]
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        check=False
    )
    return result.stdout


def parse_commits(output):
    """Parse git log output into structured data."""
    commits = []
    lines = output.split('\n')
    i = 0

    while i < len(lines):
        line = lines[i].strip()
        if not line:
            i += 1
            continue

        # Check if it's a commit header line (hash|date|author format)
        # Date format is like: 2026-01-31 01:18:49 +0800
        if '|' in line and line.count('|') >= 2:
            parts = line.split('|')
            if len(parts) >= 3:
                commit_hash = parts[0]
                # Parse date manually: format is "YYYY-MM-DD HH:MM:SS +TZ"
                date_part = parts[1].strip()
                # Split into datetime and timezone parts
                # Format: 2026-01-31 01:18:49 +0800
                if ' ' in date_part:
                    dt_str, tz_str = date_part.rsplit(' ', 1)
                    # Replace first space with T to get ISO format
                    dt_str = dt_str.replace(' ', 'T')
                    # Parse datetime without timezone for simplicity
                    commit_date = datetime.fromisoformat(dt_str)
                else:
                    commit_date = datetime.fromisoformat(date_part)
                author = parts[2]

                # Parse file changes
                i += 1
                files_changed = 0
                additions = 0
                deletions = 0

                while i < len(lines):
                    stat_line = lines[i].strip()
                    if not stat_line:
                        i += 1
                        continue
                    # Stop if we hit another commit (another line with |)
                    if '|' in stat_line and stat_line.count('|') >= 2:
                        break
                    # Parse numstat line: add del filename
                    numstat_parts = stat_line.split('\t')
                    if len(numstat_parts) >= 2:
                        try:
                            add = int(numstat_parts[0]) if numstat_parts[0] != '-' else 0
                            sub = int(numstat_parts[1]) if numstat_parts[1] != '-' else 0
                            additions += add
                            deletions += sub
                            files_changed += 1
                        except ValueError:
                            pass
                    i += 1

                commits.append({
                    'hash': commit_hash,
                    'date': commit_date,
                    'author': author,
                    'files': files_changed,
                    'additions': additions,
                    'deletions': deletions,
                })
                continue
        i += 1

    return commits


def group_by_period(commits, period):
    """Group commits by time period."""
    grouped = defaultdict(lambda: {'commits': 0, 'files': 0, 'additions': 0, 'deletions': 0, 'authors': set()})

    for commit in commits:
        date = commit['date']

        if period == 'day':
            key = date.strftime('%Y-%m-%d')
        elif period == 'week':
            # Get Monday of the week
            monday = date - timedelta(days=date.weekday())
            key = monday.strftime('%Y-W%W')
        elif period == 'month':
            key = date.strftime('%Y-%m')
        else:
            key = date.strftime('%Y')

        grouped[key]['commits'] += 1
        grouped[key]['files'] += commit['files']
        grouped[key]['additions'] += commit['additions']
        grouped[key]['deletions'] += commit['deletions']
        grouped[key]['authors'].add(commit['author'])

    return grouped


def print_stats(grouped, period_name, top_n=10):
    """Print statistics for a period."""
    if not grouped:
        print(f"\nNo {period_name} data available.")
        return

    # Sort by period key
    sorted_periods = sorted(grouped.items(), reverse=True)[:top_n]

    print(f"\n{'='*80}")
    print(f"Top {len(sorted_periods)} {period_name} (most recent first)")
    print(f"{'='*80}")
    print(f"{'Period':<15} {'Commits':<10} {'Files':<10} {'+Lines':<12} {'-Lines':<12} {'Authors':<10}")
    print(f"{'-'*80}")

    for period, stats in sorted_periods:
        print(f"{period:<15} {stats['commits']:<10} {stats['files']:<10} "
              f"{stats['additions']:<12} {stats['deletions']:<12} {len(stats['authors']):<10}")


def print_summary(commits):
    """Print overall summary."""
    if not commits:
        print("\nNo commits found.")
        return

    total_commits = len(commits)
    total_files = sum(c['files'] for c in commits)
    total_additions = sum(c['additions'] for c in commits)
    total_deletions = sum(c['deletions'] for c in commits)
    all_authors = set(c['author'] for c in commits)

    date_range = f"{commits[-1]['date'].strftime('%Y-%m-%d')} to {commits[0]['date'].strftime('%Y-%m-%d')}"

    print(f"\n{'='*80}")
    print("OVERALL SUMMARY")
    print(f"{'='*80}")
    print(f"Date Range:     {date_range}")
    print(f"Total Commits:  {total_commits}")
    print(f"Total Files:    {total_files}")
    print(f"Total Additions: {total_additions}")
    print(f"Total Deletions: {total_deletions}")
    print(f"Net Change:     {total_additions - total_deletions:+}")
    print(f"Total Authors:  {len(all_authors)}")
    print(f"{'='*80}")


def main():
    parser = argparse.ArgumentParser(
        description="Analyze git statistics by day, week, and month"
    )
    parser.add_argument(
        '--since',
        default='--1-year',
        help='Time range to analyze (default: --1-year)'
    )
    parser.add_argument(
        '--period',
        choices=['day', 'week', 'month', 'all'],
        default='all',
        help='Period to group by (default: all)'
    )
    parser.add_argument(
        '--top',
        type=int,
        default=20,
        help='Number of top periods to show (default: 20)'
    )

    args = parser.parse_args()

    # Check if we're in a git repo
    try:
        subprocess.run(['git', 'rev-parse', '--git-dir'],
                      capture_output=True, check=True)
    except subprocess.CalledProcessError:
        print("Error: Not a git repository", file=sys.stderr)
        sys.exit(1)

    print(f"Analyzing git history (since: {args.since})...")

    output = get_commit_stats(args.since)
    commits = parse_commits(output)

    if not commits:
        print("No commits found in the specified time range.")
        sys.exit(0)

    print_summary(commits)

    if args.period in ['day', 'all']:
        daily = group_by_period(commits, 'day')
        print_stats(daily, 'Daily', args.top)

    if args.period in ['week', 'all']:
        weekly = group_by_period(commits, 'week')
        print_stats(weekly, 'Weekly', args.top)

    if args.period in ['month', 'all']:
        monthly = group_by_period(commits, 'month')
        print_stats(monthly, 'Monthly', args.top)


if __name__ == '__main__':
    main()
