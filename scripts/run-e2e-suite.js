#!/usr/bin/env bun
import { spawn } from 'node:child_process';
import { readdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';

const repoRoot = path.resolve(new URL('..', import.meta.url).pathname);
const e2eDir = path.join(repoRoot, 'tests', 'e2e');
const manifestPath = path.join(e2eDir, 'e2e.manifest.json');
const runnerPath = path.join(repoRoot, 'scripts', 'run-e2e.sh');

function usage() {
  console.log(`Usage:
  bun scripts/run-e2e-suite.js [--list]
  bun scripts/run-e2e-suite.js [--group name] [--test name-or-path] [--parallel n] [--serial] [--no-build]

Examples:
  bun scripts/run-e2e-suite.js --list
  bun scripts/run-e2e-suite.js --group ui
  bun scripts/run-e2e-suite.js --test qr
  bun scripts/run-e2e-suite.js --parallel 3`);
}

function parseArgs(argv) {
  const options = {
    groups: new Set(),
    tests: new Set(),
    list: false,
    build: true,
    maxParallel: Number(process.env.OTF_E2E_PARALLEL || 2),
  };

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--help' || arg === '-h') {
      usage();
      process.exit(0);
    } else if (arg === '--list') {
      options.list = true;
    } else if (arg === '--no-build') {
      options.build = false;
    } else if (arg === '--serial') {
      options.maxParallel = 1;
    } else if (arg === '--parallel') {
      const next = Number(argv[++i]);
      if (!Number.isInteger(next) || next < 1) {
        throw new Error('--parallel requires a positive integer');
      }
      options.maxParallel = next;
    } else if (arg === '--group') {
      const next = argv[++i];
      if (!next) throw new Error('--group requires a group name');
      options.groups.add(next);
    } else if (arg === '--test') {
      const next = argv[++i];
      if (!next) throw new Error('--test requires a test name or path');
      options.tests.add(next);
    } else {
      options.tests.add(arg);
    }
  }

  return options;
}

async function loadManifest() {
  const manifest = JSON.parse(await readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.groups)) {
    throw new Error('e2e manifest must contain a groups array');
  }
  return manifest;
}

async function validateManifest(manifest) {
  const diskTests = new Set(
    (await readdir(e2eDir)).filter((file) => file.endsWith('.test.js')),
  );
  const listed = new Set();

  for (const group of manifest.groups) {
    if (!group.name || !Array.isArray(group.tests)) {
      throw new Error('each manifest group needs name and tests');
    }
    for (const file of group.tests) {
      if (listed.has(file)) {
        throw new Error(`duplicate e2e manifest entry: ${file}`);
      }
      if (!diskTests.has(file)) {
        throw new Error(`manifest references missing e2e test: ${file}`);
      }
      listed.add(file);
    }
  }

  const missing = [...diskTests].filter((file) => !listed.has(file)).sort();
  if (missing.length > 0) {
    throw new Error(`e2e tests missing from manifest: ${missing.join(', ')}`);
  }
}

function normalizeTestName(input) {
  const base = path.basename(input);
  return base.endsWith('.js') ? base : input;
}

function selectGroups(manifest, options) {
  return manifest.groups
    .filter((group) => options.groups.size === 0 || options.groups.has(group.name))
    .map((group) => {
      if (options.tests.size === 0) return group;
      const wanted = [...options.tests].map(normalizeTestName);
      return {
        ...group,
        tests: group.tests.filter((file) =>
          wanted.some((item) => file === item || file.includes(item))),
      };
    })
    .filter((group) => group.tests.length > 0);
}

function printList(groups) {
  for (const group of groups) {
    const mode = group.parallel ? 'parallel' : 'serial';
    console.log(`${group.name} (${mode})`);
    for (const testFile of group.tests) {
      console.log(`  ${testFile}`);
    }
  }
}

function runCommand(command, args, options = {}) {
  return new Promise((resolve) => {
    const child = spawn(command, args, {
      cwd: repoRoot,
      env: { ...process.env, ...options.env },
      stdio: options.stdio || 'inherit',
    });
    child.on('error', (error) => {
      console.error(error.message || error);
      resolve({ code: 1, signal: null });
    });
    child.on('exit', (code, signal) => {
      resolve({ code: code ?? 1, signal });
    });
  });
}

async function buildUi() {
  console.log('==> build:ui');
  const result = await runCommand(process.execPath, ['run', 'build:ui']);
  if (result.code !== 0) {
    throw new Error(`build:ui failed with code ${result.code}`);
  }
}

function portEnv(index) {
  const devBase = Number(process.env.OTF_E2E_PORT_BASE || 5100);
  const cdpBase = Number(process.env.OTF_E2E_CDP_PORT_BASE || 9400);
  const devPort = devBase + index;
  const cdpPort = cdpBase + index;
  return {
    OTF_E2E_DEV_PORT: String(devPort),
    OTF_E2E_STATIC_PORT: String(devPort),
    OTF_E2E_DEV_URL: `http://127.0.0.1:${devPort}`,
    OTF_E2E_CDP_PORT: String(cdpPort),
    OTF_E2E_SKIP_UI_BUILD: '1',
  };
}

async function runTest(testFile, index) {
  const relativePath = path.join('tests', 'e2e', testFile);
  console.log(`==> ${testFile}`);
  const startedAt = Date.now();
  const result = await runCommand(runnerPath, [relativePath], {
    env: portEnv(index),
  });
  return {
    testFile,
    code: result.code,
    durationMs: Date.now() - startedAt,
  };
}

async function runPool(testFiles, maxParallel, nextIndex) {
  const results = [];
  let cursor = 0;

  async function worker() {
    while (cursor < testFiles.length) {
      const testFile = testFiles[cursor];
      const index = nextIndex.value;
      cursor += 1;
      nextIndex.value += 1;
      results.push(await runTest(testFile, index));
    }
  }

  await Promise.all(
    Array.from({ length: Math.min(maxParallel, testFiles.length) }, () => worker()),
  );
  return results;
}

function printSummary(results) {
  console.log('\nE2E summary');
  for (const result of results) {
    const status = result.code === 0 ? 'PASS' : 'FAIL';
    const seconds = (result.durationMs / 1000).toFixed(1);
    console.log(`${status} ${result.testFile} (${seconds}s)`);
  }
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const manifest = await loadManifest();
  await validateManifest(manifest);
  const groups = selectGroups(manifest, options);

  if (options.groups.size > 0 && groups.length === 0) {
    throw new Error(`no matching e2e group: ${[...options.groups].join(', ')}`);
  }
  if (options.tests.size > 0 && groups.length === 0) {
    throw new Error(`no matching e2e test: ${[...options.tests].join(', ')}`);
  }

  if (options.list) {
    printList(groups);
    return;
  }

  if (options.build) {
    await buildUi();
  }

  const results = [];
  const nextIndex = { value: 0 };
  for (const group of groups) {
    console.log(`\n==> group:${group.name} ${group.parallel ? `(parallel ${options.maxParallel})` : '(serial)'}`);
    const groupResults = group.parallel
      ? await runPool(group.tests, options.maxParallel, nextIndex)
      : await runPool(group.tests, 1, nextIndex);
    results.push(...groupResults);
    if (groupResults.some((result) => result.code !== 0)) {
      break;
    }
  }

  printSummary(results);
  if (results.some((result) => result.code !== 0)) {
    process.exit(1);
  }
}

main().catch((error) => {
  console.error(error.message || error);
  process.exit(1);
});
