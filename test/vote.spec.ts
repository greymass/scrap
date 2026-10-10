import {Asset, Name, Serializer, TimePointSec, UInt64} from '@wharfkit/antelope'
import {Blockchain} from '@greymass/vert'
import {beforeEach, describe, expect, test} from 'bun:test'
import {createHash} from 'node:crypto'
import {join} from 'node:path'

const ROOT = join(import.meta.dir, '..')
const TOKEN = 'scrap'
const EON = 'eon.shipload'
const NEX = 'nex.shipload'
const EPOCH_SECONDS = 446400
const NOTICE_EPOCHS = 3
const GAME_START = new Date('2026-01-01T00:00:00.000Z')
const NOW = new Date('2026-03-01T12:00:00.000Z')
const DAY = 86400
const SCRAP_CODE = Asset.SymbolCode.from('SCRAP').value.value

type Contract = ReturnType<Blockchain['createContract']>

let bc: Blockchain
let scrap: Contract
let drops: Contract
let epochDrops: Contract
let eon: Contract

const n = (name: string) => Name.from(name).value.value

async function ready(contract: Contract) {
    while (!contract.isContract) {
        await new Promise((r) => setTimeout(r, 5))
    }
}

async function setup(balances: Record<string, number>) {
    bc = new Blockchain()
    bc.setTime(TimePointSec.from(NOW))
    bc.createAccounts(NEX, 'oracle', 'alice', 'bob', 'carol', 'dave', 'erin')
    scrap = bc.createContract(TOKEN, join(ROOT, 'build/eosio.token'), true)
    drops = bc.createContract('drops', join(ROOT, 'test/artifacts/drops'), true)
    epochDrops = bc.createContract('epoch.drops', join(ROOT, 'test/artifacts/epoch.drops'), true)
    eon = bc.createContract(EON, join(ROOT, 'test/artifacts/gamestub'), true)
    for (const contract of [scrap, drops, epochDrops, eon]) await ready(contract)

    await scrap.actions.create([TOKEN, '1000000000 SCRAP']).send(`${TOKEN}@active`)
    const total = Object.values(balances).reduce((a, b) => a + b, 0)
    await scrap.actions.issue([TOKEN, `${total} SCRAP`, '']).send(`${TOKEN}@active`)
    for (const [holder, amount] of Object.entries(balances)) {
        if (holder !== TOKEN) await transfer(TOKEN, holder, amount)
    }
    await drops.actions.enable([true]).send('drops@active')
}

async function pointEon(token: string) {
    await eon.actions.setemitcfg([token]).send(`${EON}@active`)
}

async function init() {
    await pointEon(TOKEN)
    await scrap.actions
        .init([EON, NEX, TimePointSec.from(GAME_START), EPOCH_SECONDS, NOTICE_EPOCHS])
        .send(`${TOKEN}@active`)
}

function advance(seconds: number) {
    const ms = Number(bc.timestamp.toMilliseconds()) + seconds * 1000
    bc.setTime(TimePointSec.from(new Date(ms)))
}

async function transfer(from: string, to: string, amount: number) {
    await scrap.actions.transfer([from, to, `${amount} SCRAP`, '']).send(`${from}@active`)
}

async function approve(holder: string) {
    await scrap.actions.approve([holder]).send(`${holder}@active`)
}

async function withdraw(holder: string) {
    await scrap.actions.withdraw([holder]).send(`${holder}@active`)
}

function balance(account: string): number {
    const row = scrap.tables.accounts(n(account)).getTableRow(SCRAP_CODE)
    return row ? Asset.from(row.balance).value : 0
}

function supply(): number {
    return Asset.from(scrap.tables.stat(SCRAP_CODE).getTableRow(SCRAP_CODE).supply).value
}

function vote() {
    const row = scrap.tables.vote(n(TOKEN)).getTableRows()[0]
    return {
        tally: Asset.from(row.tally).value,
        ratified: Boolean(row.ratified),
        closed: Boolean(row.closed),
        supply: Asset.from(row.supply).value,
        ratifiedEpoch: Number(row.ratified_epoch),
        startEpoch: Number(row.start_epoch),
        startTime: String(row.start_time),
    }
}

function approvals(): {account: string; snapshot: number; snapped: boolean}[] {
    return scrap.tables
        .approvals(n(TOKEN))
        .getTableRows()
        .map((row: any) => ({
            account: String(row.account),
            snapshot: Asset.from(row.snapshot).value,
            snapped: Boolean(row.snapped),
        }))
}

function payer(table: string, primaryKey: bigint): string {
    const tab = bc.store.findTable(n(TOKEN), n(TOKEN), n(table))
    return Name.from(UInt64.from(tab.get(primaryKey)!.payer)).toString()
}

function expectTallyMatchesApprovers() {
    const sum = approvals().reduce((total, row) => total + balance(row.account), 0)
    expect(vote().tally).toBe(sum)
}

function traces(action: string, receiver = TOKEN) {
    return bc.actionTraces.filter(
        (t) => String(t.action) === action && String(t.receiver.name) === receiver
    )
}

function decode(trace: any, type: string): any {
    return Serializer.objectify(
        Serializer.decode({data: trace.data, abi: (scrap as any).abi, type})
    )
}

function expectRatifiedNotice(tally: number, supplyAtCrossing: number) {
    const notices = traces('logratify', EON)
    expect(notices).toHaveLength(1)
    expect(notices[0].isNotification).toBe(true)
    const v = vote()
    const data = decode(notices[0], 'logratify')
    expect(data.tally).toBe(`${tally} SCRAP`)
    expect(data.supply).toBe(`${supplyAtCrossing} SCRAP`)
    expect(data.ratified_epoch).toBe(v.ratifiedEpoch)
    expect(data.start_epoch).toBe(v.startEpoch)
    expect(data.start_time).toBe(v.startTime)
}

function epochOn(date: Date): number {
    return Math.floor((date.getTime() - GAME_START.getTime()) / 1000 / EPOCH_SECONDS) + 1
}

function epochStart(epoch: number): string {
    const ms = GAME_START.getTime() + (epoch - 1) * EPOCH_SECONDS * 1000
    return new Date(ms).toISOString().slice(0, 19)
}

async function mintableDroplets(owner: string): Promise<string[]> {
    await epochDrops.actions.addoracle(['oracle']).send('epoch.drops@active')
    await epochDrops.actions.init([]).send('epoch.drops@active')
    await drops.actions
        .generate([owner, true, 2048, `${owner}-droplets-for-the-scrap-vote-harness`, null, null])
        .send(`${owner}@active`)
    advance(DAY)
    await epochDrops.actions.forcereveal([1, 'harness']).send('epoch.drops@active')
    const seed = String(epochDrops.tables.epoch(n('epoch.drops')).getTableRow(1n).seed)
    const owned = drops.tables
        .drop(n('drops'))
        .getTableRows(undefined, {limit: 10000})
        .filter((row: any) => String(row.owner) === owner)
        .map((row: any) => String(row.seed))
    return owned.filter((id: string) =>
        createHash('sha256')
            .update(seed + id)
            .digest('hex')
            .startsWith('00')
    )
}

async function destroy(owner: string, ids: string[]) {
    await drops.actions.destroy([owner, ids, null, TOKEN]).send(`${owner}@active`)
}

describe('holder vote', () => {
    describe('before init', () => {
        beforeEach(() => setup({alice: 400}))

        test('approve refuses until the vote opens', async () => {
            await expect(approve('alice')).rejects.toThrow('The vote has not opened.')
        })

        test('transfers work with no vote state', async () => {
            await transfer('alice', 'bob', 10)
            expect(balance('bob')).toBe(10)
        })
    })

    describe('init', () => {
        beforeEach(() => setup({alice: 400}))

        test('writes config and an empty vote once, contract-paid', async () => {
            await init()
            const config = scrap.tables.config(n(TOKEN)).getTableRows()[0]
            expect(String(config.eon)).toBe(EON)
            expect(String(config.nex)).toBe(NEX)
            expect(Number(config.epoch_seconds)).toBe(EPOCH_SECONDS)
            expect(Number(config.notice_epochs)).toBe(NOTICE_EPOCHS)
            expect(vote()).toMatchObject({tally: 0, ratified: false, closed: false})
            expect(payer('config', n('config'))).toBe(TOKEN)
            expect(payer('vote', n('vote'))).toBe(TOKEN)
            await expect(init()).rejects.toThrow('The handover is already initialized.')
        })

        test('refuses while eon has no token contract set', async () => {
            await expect(
                scrap.actions
                    .init([EON, NEX, TimePointSec.from(GAME_START), EPOCH_SECONDS, NOTICE_EPOCHS])
                    .send(`${TOKEN}@active`)
            ).rejects.toThrow('eon has not set this contract as its SCRAP token.')
        })

        test('refuses while eon names another token contract', async () => {
            await pointEon('alice')
            await expect(
                scrap.actions
                    .init([EON, NEX, TimePointSec.from(GAME_START), EPOCH_SECONDS, NOTICE_EPOCHS])
                    .send(`${TOKEN}@active`)
            ).rejects.toThrow('eon has not set this contract as its SCRAP token.')
            expect(scrap.tables.config(n(TOKEN)).getTableRows()).toEqual([])
        })

        test('requires the token account', async () => {
            await expect(
                scrap.actions
                    .init([EON, NEX, TimePointSec.from(GAME_START), EPOCH_SECONDS, 1])
                    .send('alice@active')
            ).rejects.toThrow()
        })

        test('refuses a zero notice', async () => {
            await pointEon(TOKEN)
            await expect(
                scrap.actions
                    .init([EON, NEX, TimePointSec.from(GAME_START), EPOCH_SECONDS, 0])
                    .send(`${TOKEN}@active`)
            ).rejects.toThrow('notice_epochs must be positive')
        })

        test('refuses a notice above twelve epochs', async () => {
            await pointEon(TOKEN)
            await expect(
                scrap.actions
                    .init([EON, NEX, TimePointSec.from(GAME_START), EPOCH_SECONDS, 13])
                    .send(`${TOKEN}@active`)
            ).rejects.toThrow('notice_epochs exceeds the maximum of 12')
        })

        test('accepts a notice of one and of twelve epochs', async () => {
            for (const notice of [1, 12]) {
                await setup({alice: 400})
                await pointEon(TOKEN)
                await scrap.actions
                    .init([EON, NEX, TimePointSec.from(GAME_START), EPOCH_SECONDS, notice])
                    .send(`${TOKEN}@active`)
            }
        })
    })

    describe('ratification while eon names another token contract', () => {
        beforeEach(async () => {
            await setup({alice: 400, bob: 300})
            await init()
            await pointEon('bob')
        })

        test('waits with the tally kept and announces nothing', async () => {
            await approve('alice')
            expect(vote()).toMatchObject({tally: 400, ratified: false})
            expect(scrap.tables.approvals(n(TOKEN)).getTableRows().length).toBe(1)
        })

        test('ratifies on a later approval once eon names scrap', async () => {
            await approve('alice')
            await pointEon(TOKEN)
            await approve('erin')
            expect(vote()).toMatchObject({tally: 400, ratified: true})
        })
    })

    describe('tally', () => {
        test('equals the approvers balance sum at every step', async () => {
            await setup({alice: 400, bob: 300, carol: 400, dave: 100})
            const minted = await mintableDroplets('alice')
            expect(minted.length).toBeGreaterThanOrEqual(2)
            await init()

            const steps: [string, () => Promise<void>][] = [
                ['alice approves', () => approve('alice')],
                ['dave approves', () => approve('dave')],
                ['approver sends to non-approver', () => transfer('alice', 'bob', 50)],
                ['non-approver sends to approver', () => transfer('bob', 'dave', 20)],
                ['approver sends to approver', () => transfer('alice', 'dave', 10)],
                ['dave withdraws', () => withdraw('dave')],
                ['erin approves with no balance', () => approve('erin')],
                ['non-approver sends to erin', () => transfer('carol', 'erin', 30)],
                ['the issuer approves', () => approve(TOKEN)],
                [
                    'issue to the approving issuer',
                    () =>
                        scrap.actions
                            .issue([TOKEN, '100 SCRAP', ''])
                            .send(`${TOKEN}@active`),
                ],
                [
                    'retire from the approving issuer',
                    () => scrap.actions.retire(['40 SCRAP', '']).send(`${TOKEN}@active`),
                ],
                ['Droplets mint to an approver', () => destroy('alice', [minted[0]])],
                ['erin withdraws', () => withdraw('erin')],
                ['dave approves again', () => approve('dave')],
            ]

            for (const [name, step] of steps) {
                await step()
                expectTallyMatchesApprovers()
                expect({name, ratified: vote().ratified}).toEqual({name, ratified: false})
            }
            expect(balance('alice')).toBe(400 - 50 - 10 + 8)
            expect(approvals().map((row) => row.account)).toEqual(['alice', 'dave', TOKEN])
            expect(vote().tally).toBe(balance('alice') + balance('dave') + balance(TOKEN))
        })

        test('a second created symbol cannot be issued, opened, transferred or retired', async () => {
            await setup({alice: 400, bob: 600})
            await init()
            await approve('alice')
            await scrap.actions.create([TOKEN, '1000 OTHER']).send(`${TOKEN}@active`)

            await expect(
                scrap.actions.issue([TOKEN, '100 OTHER', '']).send(`${TOKEN}@active`)
            ).rejects.toThrow('only SCRAP is supported')
            await expect(
                scrap.actions.open(['alice', '0,OTHER', 'alice']).send('alice@active')
            ).rejects.toThrow('only SCRAP is supported')
            await expect(
                scrap.actions.transfer(['alice', 'bob', '1 OTHER', '']).send('alice@active')
            ).rejects.toThrow('only SCRAP is supported')
            await expect(
                scrap.actions.retire(['1 OTHER', '']).send(`${TOKEN}@active`)
            ).rejects.toThrow('only SCRAP is supported')
            expectTallyMatchesApprovers()
        })

        test('approve twice and withdraw without approving refuse', async () => {
            await setup({alice: 400, bob: 600})
            await init()
            await approve('alice')
            await expect(approve('alice')).rejects.toThrow('This account already approves.')
            await expect(withdraw('bob')).rejects.toThrow('This account does not approve.')
        })

        test('approval rows are holder-paid', async () => {
            await setup({alice: 400, bob: 600})
            await init()
            await approve('alice')
            expect(payer('approvals', n('alice'))).toBe('alice')
        })

        test('exactly half of supply does not pass', async () => {
            await setup({alice: 500, bob: 500})
            await init()
            await approve('alice')
            expect(vote()).toMatchObject({tally: 500, ratified: false})
        })
    })

    describe('ratification', () => {
        test('latches on approve and notifies the game', async () => {
            await setup({alice: 400, bob: 300, carol: 300})
            await init()
            await approve('alice')
            await approve('bob')

            const R = epochOn(NOW)
            expect(vote()).toEqual({
                tally: 700,
                ratified: true,
                closed: false,
                supply: 1000,
                ratifiedEpoch: R,
                startEpoch: R + NOTICE_EPOCHS,
                startTime: epochStart(R + NOTICE_EPOCHS),
            })
            expectRatifiedNotice(700, 1000)
        })

        test('latches on an incoming transfer', async () => {
            await setup({alice: 400, bob: 300, carol: 300})
            await init()
            await approve('alice')
            await transfer('carol', 'alice', 101)
            expect(vote()).toMatchObject({tally: 501, ratified: true, supply: 1000})
            expectRatifiedNotice(501, 1000)
        })

        test('latches on a Droplets mint', async () => {
            await setup({alice: 500, bob: 500})
            const minted = await mintableDroplets('alice')
            await init()
            await approve('alice')
            expect(vote().ratified).toBe(false)
            await destroy('alice', [minted[0]])
            expect(vote()).toMatchObject({tally: 508, ratified: true, supply: 1008})
            expectRatifiedNotice(508, 1008)
        })

        test('latches on a retire by a non-approving issuer', async () => {
            await setup({alice: 500, bob: 400, [TOKEN]: 100})
            await init()
            await approve('alice')
            await scrap.actions.retire(['1 SCRAP', '']).send(`${TOKEN}@active`)
            expect(vote()).toMatchObject({tally: 500, ratified: true, supply: 999})
            expectRatifiedNotice(500, 999)
        })

        test('freezes the vote and snapshots supporters on their first change', async () => {
            await setup({alice: 400, bob: 300, carol: 300})
            const minted = await mintableDroplets('bob')
            await init()
            await approve('alice')
            await approve('bob')
            const frozen = vote()

            await expect(approve('carol')).rejects.toThrow(
                'The vote has passed and approvals are final.'
            )
            await expect(withdraw('alice')).rejects.toThrow(
                'The vote has passed and approvals are final.'
            )

            await transfer('alice', 'carol', 100)
            await transfer('carol', 'alice', 300)
            await destroy('bob', [minted[0]])
            await destroy('bob', [minted[1]])

            expect(vote()).toEqual(frozen)
            expect(traces('logratify', EON)).toHaveLength(0)
            expect(approvals()).toEqual([
                {account: 'alice', snapshot: 400, snapped: true},
                {account: 'bob', snapshot: 300, snapped: true},
            ])
        })
    })

    describe('closevote', () => {
        test('logs weights at crossing over pages and refunds rows', async () => {
            await setup({alice: 400, bob: 300, carol: 200, dave: 100})
            await init()
            await expect(scrap.actions.closevote([2]).send('carol@active')).rejects.toThrow(
                'The vote has not passed.'
            )
            await approve('alice')
            await approve('dave')
            await approve('bob')
            const crossing = vote().tally
            expect(crossing).toBe(800)

            await transfer('alice', 'carol', 150)
            await transfer('carol', 'dave', 10)
            await transfer('bob', 'alice', 50)

            const logged: {account: string; weight: string}[] = []
            const pages: boolean[] = []
            for (let page = 0; page < 2; page++) {
                await scrap.actions.closevote([2]).send('carol@active')
                const [log] = traces('logsupport')
                const data = decode(log, 'logsupport')
                logged.push(...data.supporters)
                pages.push(data.closed)
                expect(vote().closed).toBe(data.closed)
            }

            expect(pages).toEqual([false, true])
            expect(logged).toEqual([
                {account: 'alice', weight: '400 SCRAP'},
                {account: 'bob', weight: '300 SCRAP'},
                {account: 'dave', weight: '100 SCRAP'},
            ])
            expect(logged.reduce((sum, row) => sum + Asset.from(row.weight).value, 0)).toBe(
                crossing
            )
            expect(approvals()).toHaveLength(0)
            expect(vote().tally).toBe(crossing)
            await expect(scrap.actions.closevote([2]).send('carol@active')).rejects.toThrow(
                'The vote is already closed.'
            )
            await transfer('alice', 'bob', 1)
            expect(vote().tally).toBe(crossing)
        })

        test('logs the live balance of a supporter who never moved', async () => {
            await setup({alice: 600, bob: 400})
            await init()
            await approve('alice')
            await scrap.actions.closevote([10]).send('bob@active')
            const data = decode(traces('logsupport')[0], 'logsupport')
            expect(data).toEqual({
                supporters: [{account: 'alice', weight: '600 SCRAP'}],
                closed: true,
            })
        })
    })

    describe('Droplets after SCRAP_START', () => {
        test('a destroy mints nothing with no epoch.drops state', async () => {
            await setup({alice: 600, bob: 400})
            await init()
            await approve('alice')
            const {startTime} = vote()

            await drops.actions
                .generate(['bob', true, 4, 'bob-droplets-after-the-handover-flip', null, null])
                .send('bob@active')
            const ids = drops.tables
                .drop(n('drops'))
                .getTableRows()
                .map((row: any) => String(row.seed))

            await expect(destroy('bob', [ids[0]])).rejects.toThrow()

            const untilStart =
                (new Date(`${startTime}Z`).getTime() - Number(bc.timestamp.toMilliseconds())) /
                1000
            advance(untilStart)
            const supplyBefore = supply()
            await destroy('bob', [ids[0], ids[1]])

            expect(supply()).toBe(supplyBefore)
            expect(balance('bob')).toBe(400)
            expect(traces('logmint')).toHaveLength(0)
            expect(drops.tables.drop(n('drops')).getTableRows()).toHaveLength(2)
        })

        test('Droplets keep minting during the notice', async () => {
            await setup({alice: 600, bob: 400})
            const minted = await mintableDroplets('bob')
            await init()
            await approve('alice')
            await destroy('bob', [minted[0]])
            expect(balance('bob')).toBe(408)
            expect(traces('logmint')).toHaveLength(1)
        })
    })
})
