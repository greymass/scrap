import {Asset, Name, Serializer, TimePointSec, UInt64} from '@wharfkit/antelope'
import {Blockchain} from '@greymass/vert'
import {beforeEach, describe, expect, test} from 'bun:test'
import {join} from 'node:path'

const ROOT = join(import.meta.dir, '..')
const TOKEN = 'scrap'
const EON = 'eon.shipload'
const NEX = 'nex.shipload'
const EPOCH_SECONDS = 446400
const NOTICE_EPOCHS = 3
const GAME_START = new Date('2026-01-01T00:00:00.000Z')
const NOW = new Date('2026-03-01T12:00:00.000Z')
const SCRAP_CODE = Asset.SymbolCode.from('SCRAP').value.value
const MAX = 1_000_000_000
const X = 1_245_000
const HOLDERS = [TOKEN, NEX, 'alice', 'bob', 'carol']

type Contract = ReturnType<Blockchain['createContract']>

let bc: Blockchain
let scrap: Contract
let eon: Contract
let nex: Contract

const n = (name: string) => Name.from(name).value.value

async function ready(contract: Contract) {
    while (!contract.isContract) {
        await new Promise((r) => setTimeout(r, 5))
    }
}

async function setup(balances: Record<string, number> = {alice: 600, bob: 400}) {
    bc = new Blockchain()
    bc.setTime(TimePointSec.from(NOW))
    bc.createAccounts('alice', 'bob', 'carol')
    scrap = bc.createContract(TOKEN, join(ROOT, 'build/eosio.token'), true)
    eon = bc.createContract(EON, join(ROOT, 'test/artifacts/gamestub'), true)
    nex = bc.createContract(NEX, join(ROOT, 'test/artifacts/gamestub'), true)
    for (const contract of [scrap, eon, nex]) await ready(contract)
    await eon.actions.setemitcfg([TOKEN]).send(`${EON}@active`)

    await scrap.actions.create([TOKEN, `${MAX} SCRAP`]).send(`${TOKEN}@active`)
    const total = Object.values(balances).reduce((a, b) => a + b, 0)
    await issue(total)
    for (const [holder, amount] of Object.entries(balances)) {
        await scrap.actions.transfer([TOKEN, holder, `${amount} SCRAP`, '']).send(`${TOKEN}@active`)
    }
}

async function init(start = GAME_START) {
    await nex.actions.setgame([EON, EPOCH_SECONDS, TimePointSec.from(start)]).send(`${NEX}@active`)
    await scrap.actions.init([EON, NEX, NOTICE_EPOCHS]).send(`${TOKEN}@active`)
}

async function ratify(): Promise<number> {
    await scrap.actions.approve(['alice']).send('alice@active')
    const row = scrap.tables.vote(n(TOKEN)).getTableRows()[0]
    expect(Boolean(row.ratified)).toBe(true)
    return Number(row.start_epoch)
}

async function issue(amount: number) {
    await scrap.actions.issue([TOKEN, `${amount} SCRAP`, '']).send(`${TOKEN}@active`)
}

async function setcurve(bands: [number, number][], ramp: number, auth = `${TOKEN}@active`) {
    await scrap.actions
        .setcurve([
            bands.map(([below, per]) => ({below: `${below} SCRAP`, per_epoch: `${per} SCRAP`})),
            ramp,
        ])
        .send(auth)
}

function epochOn(date: Date): number {
    return Math.floor((date.getTime() - GAME_START.getTime()) / 1000 / EPOCH_SECONDS) + 1
}

function at(epoch: number, offset = 60) {
    const ms = GAME_START.getTime() + ((epoch - 1) * EPOCH_SECONDS + offset) * 1000
    bc.setTime(TimePointSec.from(new Date(ms)))
}

async function commit(closed: number, opened: number, amount: number | string) {
    const quantity = typeof amount === 'number' ? `${amount} SCRAP` : amount
    await eon.actions.sendcommit([TOKEN, closed, opened, quantity]).send(`${EON}@active`)
}

async function close(closed: number, opened: number, amount: number | string = 0) {
    at(opened)
    await commit(closed, opened, amount)
}

async function mintshare(player: string, amount: number) {
    await eon.actions.sendmint([TOKEN, player, `${amount} SCRAP`]).send(`${EON}@active`)
}

async function walkTo(start: number, from = epochOn(NOW)) {
    for (let closed = from; closed < start; closed++) await close(closed, closed + 1)
}

function emission() {
    const row = scrap.tables.emission(n(TOKEN)).getTableRows()[0]
    return {
        owed: Asset.from(row.owed).value,
        committedEpoch: Number(row.committed_epoch),
        committed: Asset.from(row.committed).value,
        targetEpoch: Number(row.target_epoch),
        target: Asset.from(row.target).value,
    }
}

function balance(account: string): number {
    const row = scrap.tables.accounts(n(account)).getTableRow(SCRAP_CODE)
    return row ? Asset.from(row.balance).value : 0
}

function supply(): number {
    return Asset.from(scrap.tables.stat(SCRAP_CODE).getTableRow(SCRAP_CODE).supply).value
}

function payer(table: string, primaryKey: bigint): string {
    const tab = bc.store.findTable(n(TOKEN), n(TOKEN), n(table))
    return Name.from(UInt64.from(tab.get(primaryKey)!.payer)).toString()
}

function traces(action: string, receiver = TOKEN) {
    return bc.actionTraces.filter(
        (t) => String(t.action) === action && String(t.receiver.name) === receiver
    )
}

function announced(): {closed: number; opened: number; target: number}[] {
    return traces('logtarget', EON).map((t: any) => {
        expect(t.isNotification).toBe(true)
        const data = Serializer.objectify(
            Serializer.decode({data: t.data, abi: (scrap as any).abi, type: 'logtarget'})
        )
        return {
            closed: Number(data.closed_epoch),
            opened: Number(data.opened_epoch),
            target: Asset.from(data.target).value,
        }
    })
}

function reported(): {closed: number; accepted: number}[] {
    return traces('logcommit', EON).map((t: any) => {
        expect(t.isNotification).toBe(true)
        const data = Serializer.objectify(
            Serializer.decode({data: t.data, abi: (scrap as any).abi, type: 'logcommit'})
        )
        return {closed: Number(data.closed_epoch), accepted: Asset.from(data.accepted).value}
    })
}

function expectInvariants() {
    expect(supply()).toBe(HOLDERS.reduce((sum, holder) => sum + balance(holder), 0))
    expect(supply() + emission().owed).toBeLessThanOrEqual(MAX)
}

describe('emission', () => {
    describe('init', () => {
        test('writes the launch curve and an empty emission row, contract-paid', async () => {
            await setup()
            await init()
            const curve = scrap.tables.curve(n(TOKEN)).getTableRows()[0]
            expect(Serializer.objectify(curve)).toEqual({
                bands: [
                    {below: '300000000 SCRAP', per_epoch: '1245000 SCRAP'},
                    {below: '600000000 SCRAP', per_epoch: '622500 SCRAP'},
                    {below: '1000000000 SCRAP', per_epoch: '311250 SCRAP'},
                ],
                ramp_epochs: 12,
            })
            expect(emission()).toEqual({
                owed: 0,
                committedEpoch: 0,
                committed: 0,
                targetEpoch: 0,
                target: 0,
            })
            expect(payer('curve', n('curve'))).toBe(TOKEN)
            expect(payer('emission', n('emission'))).toBe(TOKEN)
        })
    })

    describe('commit', () => {
        beforeEach(async () => {
            await setup()
            await init()
        })

        test('before ratification changes nothing and announces nothing', async () => {
            const R = epochOn(NOW)
            await close(R, R + 1, 500)
            expect(announced()).toEqual([])
            expect(emission()).toMatchObject({owed: 0, committedEpoch: 0, targetEpoch: 0})
        })

        test('accepts the commit for a just-ended game epoch and announces a target when the game starts a few minutes off the hour', async () => {
            await setup()
            const start = new Date(GAME_START.getTime() + 300_000)
            await init(start)
            const R = Math.floor((NOW.getTime() - start.getTime()) / 1000 / EPOCH_SECONDS) + 1
            await ratify()
            bc.setTime(TimePointSec.from(new Date(start.getTime() + (R * EPOCH_SECONDS + 60) * 1000)))
            await commit(R, R + 1, 0)
            expect(announced().map((a) => a.opened)).toEqual([R + 1])
            expect(emission().targetEpoch).toBe(R + 1)
        })

        test('ramps from SCRAP_START in twelve steps', async () => {
            const R = epochOn(NOW)
            const start = await ratify()
            expect(start).toBe(R + NOTICE_EPOCHS)

            let owed = 0
            let previous = 0
            for (let closed = R; closed < start + 14; closed++) {
                const opened = closed + 1
                await close(closed, opened, previous)
                const step = opened - start + 1
                const expected = step < 1 ? 0 : step < 12 ? (X * step) / 12 : X
                expect({opened, target: announced()}).toEqual({
                    opened,
                    target: [{closed, opened, target: expected}],
                })
                owed += closed === R ? 0 : previous
                expect(emission()).toEqual({
                    owed,
                    committedEpoch: closed === R ? 0 : closed,
                    committed: closed === R ? 0 : previous,
                    targetEpoch: opened,
                    target: expected,
                })
                previous = expected
            }
            expect(emission().target).toBe(X)
            expectInvariants()
        })

        test('steps down when issued SCRAP crosses a band', async () => {
            const start = await ratify()
            await setcurve(
                [
                    [300_000_000, X],
                    [600_000_000, X / 2],
                    [MAX, X / 4],
                ],
                1
            )
            await issue(298_754_000)
            await walkTo(start)
            expect(emission().target).toBe(X)

            await close(start, start + 1, X)
            expect(supply() + emission().owed).toBe(300_000_000)
            expect(announced()[0].target).toBe(X / 2)
            expectInvariants()
        })

        test('stops a target at the band edge it would cross', async () => {
            const start = await ratify()
            await setcurve(
                [
                    [300_000_000, X],
                    [600_000_000, X / 2],
                    [MAX, X / 4],
                ],
                1
            )
            await issue(299_500_000)
            await walkTo(start)
            expect(300_000_000 - (supply() + emission().owed)).toBe(499_000)
            expect(emission().target).toBe(499_000)

            await close(start, start + 1, 499_000)
            expect(supply() + emission().owed).toBe(300_000_000)
            expect(announced()[0].target).toBe(X / 2)
            expectInvariants()
        })

        test('pays only the remainder in the final epoch below 1B', async () => {
            const start = await ratify()
            await setcurve(
                [
                    [300_000_000, X],
                    [600_000_000, X / 2],
                    [MAX, X / 4],
                ],
                1
            )
            await issue(999_800_000)
            await walkTo(start)
            const remainder = MAX - supply()
            expect(remainder).toBe(199_000)
            expect(emission().target).toBe(remainder)

            await close(start, start + 1, 500_000)
            expect(emission()).toMatchObject({owed: remainder, committed: remainder, target: 0})
            expect(supply() + emission().owed).toBe(MAX)

            await close(start + 1, start + 2, 5)
            expect(emission()).toMatchObject({owed: remainder, committed: 0, target: 0})
            await expect(issue(1)).rejects.toThrow('quantity exceeds available supply')

            await mintshare('alice', remainder)
            expect(supply()).toBe(MAX)
            expect(emission().owed).toBe(0)
            expectInvariants()
        })

        test('accepts the last announced epoch after an outage and skips the rest', async () => {
            const start = await ratify()
            await walkTo(start)
            await close(start, start + 1, emission().target)
            const target = emission().target

            await close(start + 1, start + 5, target)
            expect(emission()).toMatchObject({committedEpoch: start + 1, committed: target})
            expect(announced()).toEqual([{closed: start + 1, opened: start + 5, target: (X * 6) / 12}])

            const owed = emission().owed
            await commit(start + 2, start + 3, 1_000)
            expect(emission()).toMatchObject({owed, committedEpoch: start + 1, targetEpoch: start + 5})

            await close(start + 5, start + 6, emission().target)
            expect(emission().committedEpoch).toBe(start + 5)
            expectInvariants()
        })

        test('announces partway up the ramp after an outage across SCRAP_START', async () => {
            const R = epochOn(NOW)
            const start = await ratify()
            await close(R, R + 1)
            await close(R + 1, start + 2)
            expect(announced()).toEqual([{closed: R + 1, opened: start + 2, target: (X * 3) / 12}])
        })

        test.each([
            ['-5 SCRAP', 0],
            ['5.0000 EOS', 0],
            ['4611686018427387903 SCRAP', X / 12],
        ])('clamps %s on the accepting path', async (amount, accepted) => {
            const start = await ratify()
            await walkTo(start)
            await close(start, start + 1, amount)
            expect(emission()).toMatchObject({owed: accepted, committed: accepted, committedEpoch: start})
            expectInvariants()
        })

        test('clamps a commit for an epoch it never announced to 0', async () => {
            const start = await ratify()
            await walkTo(start + 1)
            const owed = emission().owed
            await close(start + 2, start + 3, 1_000)
            expect(emission()).toMatchObject({owed, committedEpoch: start, targetEpoch: start + 3})
        })

        test('accepts nothing from a repeated commit', async () => {
            const start = await ratify()
            await walkTo(start)
            const target = emission().target
            await close(start, start + 1, target)
            const after = emission()

            await commit(start, start + 1, target)
            expect(emission()).toEqual(after)
            expect(announced()).toEqual([])
            expect(reported()).toEqual([])
        })

        test('reports the amount it accepted for the closed epoch to the game when the opened epoch is rejected', async () => {
            const start = await ratify()
            await walkTo(start)
            const target = emission().target
            const left = target / 2
            await issue(MAX - supply() - emission().owed - left)

            at(start + 1)
            await commit(start, start + 2, target)
            expect(emission()).toMatchObject({committedEpoch: start, committed: left, targetEpoch: start})
            expect(announced()).toEqual([])
            expect(reported()).toEqual([{closed: start, accepted: left}])
            expectInvariants()
        })

        test('clamps a commit for an epoch still open on its own clock to 0', async () => {
            const start = await ratify()
            await walkTo(start)
            const before = emission()

            at(start, EPOCH_SECONDS - 1)
            await commit(start, start + 1, before.target)
            expect(emission()).toEqual(before)
            expect(announced()).toEqual([])
            expect(reported()).toEqual([{closed: start, accepted: 0}])

            at(start + 1)
            await commit(start, start + 1, before.target)
            expect(emission()).toMatchObject({owed: before.target, committedEpoch: start})
        })

        test('clamps an over-target commit to the target', async () => {
            const start = await ratify()
            await walkTo(start)
            const target = emission().target
            await close(start, start + 1, target + 1_000_000)
            expect(emission()).toMatchObject({owed: target, committed: target})
        })

        test('never throws on any input from the game', async () => {
            const start = await ratify()
            await walkTo(start)
            const inputs: [number, number, string][] = [
                [0, 0, '0 SCRAP'],
                [start, start - 1, '10 SCRAP'],
                [start, start, '10 SCRAP'],
                [4294967295, 4294967295, '10 SCRAP'],
                [start, 4294967295, '4611686018427387903 SCRAP'],
                [start, start + 1, '-5 SCRAP'],
                [start, start + 1, '5.0000 EOS'],
                [start, start + 1, '4611686018427387903 SCRAP'],
                [start - 1, start + 1, '10 SCRAP'],
            ]
            at(start + 1)
            for (const [closed, opened, amount] of inputs) {
                await commit(closed, opened, amount)
                expectInvariants()
            }
            expect(emission().owed).toBeLessThanOrEqual(X / 12)
        })

        test('never throws before init', async () => {
            await setup()
            await commit(1, 2, '10 SCRAP')
            expect(scrap.tables.emission(n(TOKEN)).getTableRows()).toEqual([])
        })

        test('completes without effect when anyone but the game sends it', async () => {
            const start = await ratify()
            await walkTo(start)
            const before = emission()
            const relay = bc.createContract('relay', join(ROOT, 'test/artifacts/gamestub'), true)
            await ready(relay)
            at(start + 1)
            await relay.actions.sendcommit([TOKEN, start, start + 1, `${X} SCRAP`]).send('relay@active')
            await scrap.actions.commit([start, start + 1, `${X} SCRAP`]).send('alice@active')
            await scrap.actions.commit([start, start + 1, `${X} SCRAP`]).send(`${EON}@active`)
            expect(emission()).toEqual(before)
            expect(announced()).toEqual([])
            expectInvariants()
        })
    })

    describe('setcurve', () => {
        beforeEach(async () => {
            await setup()
            await init()
        })

        test('applies from the next announcement', async () => {
            const start = await ratify()
            await walkTo(start)
            expect(emission().target).toBe(X / 12)

            await setcurve([[MAX, 2 * X]], 12)
            expect(emission().target).toBe(X / 12)

            await close(start, start + 1, X / 12)
            expect(emission().target).toBe((2 * X * 2) / 12)
        })

        test('requires the token account and a valid curve', async () => {
            await expect(setcurve([[MAX, X]], 12, 'alice@active')).rejects.toThrow()
            await expect(
                setcurve(
                    [
                        [600_000_000, X],
                        [300_000_000, X],
                    ],
                    12
                )
            ).rejects.toThrow('Band edges must increase.')
            await expect(setcurve([[MAX + 1, X]], 12)).rejects.toThrow(
                'A band edge exceeds the maximum supply.'
            )
            await expect(setcurve([[MAX, X]], 0)).rejects.toThrow('ramp_epochs must be positive')
        })

        test('refuses a last band edge below the maximum supply', async () => {
            await expect(setcurve([[600_000_000, X]], 12)).rejects.toThrow(
                'The last band edge must equal the maximum supply.'
            )
        })

        test('refuses a band with no target', async () => {
            await expect(
                setcurve(
                    [
                        [300_000_000, 0],
                        [MAX, X],
                    ],
                    12
                )
            ).rejects.toThrow('A band target must be positive.')
        })
    })

    describe('mintshare', () => {
        beforeEach(async () => {
            await setup()
            await init()
        })

        test('mints owed SCRAP into the platform balance and notifies it', async () => {
            const start = await ratify()
            await walkTo(start)
            await close(start, start + 1, X / 12)
            const before = supply()

            await mintshare('carol', 1_000)
            expect(emission().owed).toBe(X / 12 - 1_000)
            expect(supply()).toBe(before + 1_000)
            expect(balance(NEX)).toBe(1_000)
            expect(balance('carol')).toBe(0)

            const [notice] = traces('mintshare', NEX)
            expect(notice.isNotification).toBe(true)
            const data = Serializer.objectify(
                Serializer.decode({data: notice.data, abi: (scrap as any).abi, type: 'mintshare'})
            )
            expect(data).toEqual({player: 'carol', quantity: '1000 SCRAP'})
            expectInvariants()
        })

        test('refuses a Share above owed', async () => {
            await expect(mintshare('carol', 1)).rejects.toThrow(
                'The Share exceeds the SCRAP owed to Shares.'
            )
            const start = await ratify()
            await walkTo(start)
            await close(start, start + 1, X / 12)
            await expect(mintshare('carol', X / 12 + 1)).rejects.toThrow(
                'The Share exceeds the SCRAP owed to Shares.'
            )
            await mintshare('carol', X / 12)
            expect(emission().owed).toBe(0)
            expectInvariants()
        })

        test('is refused from anyone but the game', async () => {
            const relay = bc.createContract('relay', join(ROOT, 'test/artifacts/gamestub'), true)
            await ready(relay)
            await expect(
                relay.actions.sendmint([TOKEN, 'carol', '1 SCRAP']).send('relay@active')
            ).rejects.toThrow('Only the game mints Shares.')
            await expect(
                scrap.actions.mintshare(['carol', '1 SCRAP']).send('alice@active')
            ).rejects.toThrow('Only the game mints Shares.')
            await expect(
                scrap.actions.mintshare(['carol', '1 SCRAP']).send(`${EON}@active`)
            ).rejects.toThrow('Only the game mints Shares.')
        })
    })
})
