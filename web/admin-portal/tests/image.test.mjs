import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import test from 'node:test';

test('the built image renders a listening HTTPS server as its unprivileged user',
    { skip: !process.env.IMAGE_REF }, () => {
        const values = JSON.parse(process.env.DEPLOY_VALUES);
        const identity = values.podSecurityContext;
        const result = spawnSync('docker', [
            'run', '--rm', '--network', 'none', '--read-only',
            '--user', `${identity.runAsUser}:${identity.runAsGroup}`,
            '--tmpfs', '/tmp',
            '--env', `PORTAL_HTTPS_PORT=${values.service.port}`,
            process.env.IMAGE_REF, 'nginx', '-T',
        ], { encoding: 'utf8' });
        assert.equal(result.status, 0, result.stderr);
        assert.ok(result.stdout.includes(`listen ${values.service.port} ssl;`),
            'the entrypoint must render the HTTPS server, not start Nginx without a listener');
    });
