#!/usr/bin/env bun
// Interactive release helper.
//
//   bun run release
//
// Prompts for the version, the pre-release type (if any), and a commit
// message; updates package.json and CMakeLists.txt; commits; tags; pushes.
// Refuses to run if the working tree has uncommitted changes so a release
// can never bundle stray edits.

import { createInterface } from 'node:readline/promises';
import { readFileSync, writeFileSync } from 'node:fs';
import { execSync } from 'node:child_process';

const COLOR = {
  reset: '\x1b[0m',
  dim: '\x1b[2m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  cyan: '\x1b[36m',
  bold: '\x1b[1m',
};

const rl = createInterface({ input: process.stdin, output: process.stdout });
const ask = async (q, def = '') => {
  const suffix = def ? ` ${COLOR.dim}[${def}]${COLOR.reset}` : '';
  const a = (await rl.question(`${q}${suffix}: `)).trim();
  return a || def;
};
const askYesNo = async (q, def = false) => {
  const ans = (await ask(`${q} (y/N)`, def ? 'y' : 'n')).toLowerCase();
  return ans === 'y' || ans === 'yes';
};

const die = (msg) => {
  console.error(`${COLOR.red}✗ ${msg}${COLOR.reset}`);
  rl.close();
  process.exit(1);
};

const sh = (cmd, opts = {}) =>
  execSync(cmd, { encoding: 'utf8', stdio: opts.stdio ?? 'pipe', ...opts }).trim();

// ── Pre-flight checks ───────────────────────────────────────────────────
try {
  sh('git rev-parse --git-dir');
} catch {
  die('not a git repo');
}

const dirty = sh('git status --porcelain');
if (dirty) {
  console.error(`${COLOR.red}✗ working tree has uncommitted changes:${COLOR.reset}`);
  console.error(dirty);
  die('commit or stash them first');
}

const branch = sh('git symbolic-ref --short HEAD');
console.log(`${COLOR.dim}On branch ${branch}${COLOR.reset}`);

// ── Read current versions ───────────────────────────────────────────────
const pkgPath = 'package.json';
const cmakePath = 'CMakeLists.txt';
const pkg = JSON.parse(readFileSync(pkgPath, 'utf8'));
const cmakeText = readFileSync(cmakePath, 'utf8');
const cmakeMatch = cmakeText.match(/project\(\s*otf-browser\s+VERSION\s+(\d+\.\d+\.\d+)/);
if (!cmakeMatch) die(`could not find 'project(otf-browser VERSION X.Y.Z ...)' in ${cmakePath}`);

console.log(`${COLOR.dim}Current package.json:  ${pkg.version}${COLOR.reset}`);
console.log(`${COLOR.dim}Current CMakeLists.txt base: ${cmakeMatch[1]}${COLOR.reset}`);

// ── Prompts ─────────────────────────────────────────────────────────────
const semverRe = /^\d+\.\d+\.\d+$/;
let baseVersion;
while (true) {
  baseVersion = await ask(`${COLOR.cyan}Version${COLOR.reset} (X.Y.Z)`, cmakeMatch[1]);
  if (semverRe.test(baseVersion)) break;
  console.error(`${COLOR.red}  → '${baseVersion}' isn't X.Y.Z; try again${COLOR.reset}`);
}

const isPre = await askYesNo(`${COLOR.cyan}Pre-release?${COLOR.reset}`, true);

let prereleaseTag = '';
if (isPre) {
  while (true) {
    const choice = (await ask(`${COLOR.cyan}Pre-release type${COLOR.reset} (1) alpha  (2) beta`, '1')).toLowerCase();
    if (choice === '1' || choice === 'alpha') { prereleaseTag = 'alpha'; break; }
    if (choice === '2' || choice === 'beta')  { prereleaseTag = 'beta'; break; }
    console.error(`${COLOR.red}  → pick 1 (alpha) or 2 (beta)${COLOR.reset}`);
  }
}

// Auto-detect next pre-release counter from existing tags.
let prereleaseCounter = 1;
if (isPre) {
  const existing = sh(`git tag --list "v${baseVersion}-${prereleaseTag}.*"`)
    .split('\n')
    .filter(Boolean)
    .map((t) => Number.parseInt(t.split('.').pop(), 10))
    .filter((n) => Number.isInteger(n));
  if (existing.length > 0) prereleaseCounter = Math.max(...existing) + 1;
  const suggested = String(prereleaseCounter);
  const answer = await ask(`${COLOR.cyan}${prereleaseTag} counter${COLOR.reset}`, suggested);
  const parsed = Number.parseInt(answer, 10);
  if (!Number.isInteger(parsed) || parsed < 1) die(`'${answer}' isn't a positive integer`);
  prereleaseCounter = parsed;
}

const fullVersion = isPre
  ? `${baseVersion}-${prereleaseTag}.${prereleaseCounter}`
  : baseVersion;
const tagName = `v${fullVersion}`;

// Refuse to overwrite an existing tag.
try {
  sh(`git rev-parse -q --verify refs/tags/${tagName}`);
  die(`tag ${tagName} already exists`);
} catch (e) {
  if (e.status !== 1 && !String(e.message).includes('not a valid object name')) {
    // status 1 from rev-parse means "not found" — exactly what we want.
    // Anything else is a real error.
  }
}

console.log('');
console.log(`${COLOR.bold}Plan${COLOR.reset}`);
console.log(`  ${COLOR.dim}package.json version  →${COLOR.reset} ${fullVersion}`);
console.log(`  ${COLOR.dim}CMakeLists.txt base   →${COLOR.reset} ${baseVersion}  ${COLOR.dim}(CMake VERSION doesn't accept hyphens; CI overrides with full string via OTF_VERSION env)${COLOR.reset}`);
console.log(`  ${COLOR.dim}Git tag               →${COLOR.reset} ${tagName}`);
console.log(`  ${COLOR.dim}Push target           →${COLOR.reset} origin ${branch}  (with --follow-tags)`);
console.log('');

const commitMsg = await ask(
  `${COLOR.cyan}Commit message${COLOR.reset}`,
  `chore: release ${fullVersion}`
);

const proceed = await askYesNo(`${COLOR.bold}Proceed?${COLOR.reset}`, false);
if (!proceed) die('aborted by user');

// ── Mutations ───────────────────────────────────────────────────────────
console.log('');
pkg.version = fullVersion;
writeFileSync(pkgPath, JSON.stringify(pkg, null, 2) + '\n');
console.log(`${COLOR.green}✓${COLOR.reset} wrote ${pkgPath}`);

const newCmake = cmakeText.replace(
  /project\(\s*otf-browser\s+VERSION\s+\d+\.\d+\.\d+/,
  `project(otf-browser VERSION ${baseVersion}`
);
writeFileSync(cmakePath, newCmake);
console.log(`${COLOR.green}✓${COLOR.reset} wrote ${cmakePath}`);

// Stage, commit, tag, push. Use stdio: 'inherit' from here so the user sees
// git's output in real time and can spot pre-commit hook failures.
const run = (cmd) => {
  console.log(`${COLOR.dim}$ ${cmd}${COLOR.reset}`);
  execSync(cmd, { stdio: 'inherit' });
};

run(`git add ${pkgPath} ${cmakePath}`);
run(`git commit -m ${JSON.stringify(commitMsg)}`);
run(`git tag -a ${tagName} -m ${JSON.stringify(`Release ${tagName}`)}`);
run(`git push --follow-tags origin ${branch}`);

console.log('');
console.log(`${COLOR.green}${COLOR.bold}✓ Released ${tagName}${COLOR.reset}`);
console.log(`${COLOR.dim}  Watch the CI build at:  $(git remote get-url origin)/actions${COLOR.reset}`);

rl.close();
