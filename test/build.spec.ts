import {describe, expect, test} from 'bun:test'
import {readFileSync} from 'node:fs'
import {join} from 'node:path'

const ROOT = join(import.meta.dir, '..')
const DEPLOY_TARGETS = ['devnet', 'testnet', 'mainnet']
const TEST_ONLY_ACTIONS = ['destroy']

function dryRun(target: string): string {
    const result = Bun.spawnSync(['make', '-n', '-C', ROOT, target])
    return result.stdout.toString()
}

describe('production build', () => {
    test('the compiled ABI lists no test-only action', () => {
        const abi = JSON.parse(readFileSync(join(ROOT, 'build/eosio.token.abi'), 'utf8'))
        const actions = abi.actions.map((action: {name: string}) => action.name)
        for (const name of TEST_ONLY_ACTIONS) {
            expect(actions).not.toContain(name)
        }
    })

    for (const target of DEPLOY_TARGETS) {
        test(`the ${target} target deploys a build without the debug flag`, () => {
            const plan = dryRun(target)
            expect(plan).toContain('set contract')
            expect(plan).not.toContain('-D DEBUG')
        })
    }
})
