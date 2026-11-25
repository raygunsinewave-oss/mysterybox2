// Mystery Box Synthesizer by Raygun
#include "GameWorld.h"
#include <algorithm>
#include <cmath>

namespace mbx
{
namespace
{
constexpr float kSpinDuration = 4.0f;
constexpr float kResultTimeout = 8.0f;
constexpr uint32_t kGreen = 0xff39ff14, kCyan = 0xff00e5ff, kGold = 0xffffc233, kRed = 0xffff4444;

inline float easeOutCubic (float t) { t = std::clamp (t, 0.0f, 1.0f); const float u = 1.0f - t; return 1.0f - u * u * u; }
} // namespace

GameWorld::GameWorld (GameHost& h) : host (h)
{
    rngState ^= (uint32_t) reinterpret_cast<uintptr_t> (this);
    zombies.reserve (64);
    particles.reserve (512);
}

float GameWorld::rand01() noexcept
{
    rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
    return (float) (rngState & 0xFFFFFF) / 16777216.0f;
}

float GameWorld::difficultyHealth() const
{
    static constexpr float m[4] = { 0.7f, 1.0f, 1.35f, 1.8f };
    return m[std::clamp (host.gameSettings().difficulty, 0, 3)];
}
float GameWorld::difficultySpeed() const
{
    static constexpr float m[4] = { 0.8f, 1.0f, 1.15f, 1.35f };
    return m[std::clamp (host.gameSettings().difficulty, 0, 3)];
}

int GameWorld::getSpinCost() const
{
    const auto& s = host.gameSettings();
    return s.freeSpin ? 0 : s.spinCost;
}

bool GameWorld::canAffordSpin() const { return host.gamePoints() >= getSpinCost(); }

void GameWorld::reset (bool keepRound)
{
    zombies.clear(); particles.clear(); texts.clear(); drops.clear();
    if (! keepRound) round = 1;
    boards = kMaxBoards; ammo = kMagSize; reloadTimer = 0;
    roundState = RoundState::Break; roundTimer = 3.0f;
    doublePointsTime = instaKillTime = 0.0f;
    toSpawn = spawned = 0;
}

void GameWorld::startRound()
{
    roundState = RoundState::Active;
    toSpawn = std::min (80, 6 + 4 * (round - 1));
    spawned = 0;
    spawnTimer = 0.5f;
    roundBanner = 2.5f;
    host.gameSfx (Sfx::RoundStart, 60, 0.8f, 0);
}

void GameWorld::spawnZombie()
{
    Zombie z;
    z.seed = (uint32_t) (rand01() * 1.0e9f);
    // Composition by round
    const float r = rand01();
    if (round >= 15 && r < 0.15f) z.type = ZType::Crawler;
    else if (round >= 10 && r < 0.25f) z.type = ZType::Brute;
    else if (round >= 5 && r < 0.25f + 0.02f * (float) std::min (round, 20)) z.type = ZType::Runner;
    else z.type = ZType::Walker;

    float hp = 1.0f + 0.4f * (float) (std::min (round, 9) - 1);
    if (round > 9) hp *= std::pow (1.1f, (float) (round - 9));
    hp *= difficultyHealth();
    float speed = 17.0f + 1.2f * (float) std::min (round, 20);
    switch (z.type)
    {
        case ZType::Walker:  z.scale = 0.9f + rand01() * 0.2f; break;
        case ZType::Runner:  speed *= 2.4f; hp *= 0.8f; z.scale = 0.85f + rand01() * 0.15f; break;
        case ZType::Brute:   speed *= 0.65f; hp *= 4.0f; z.scale = 1.45f; break;
        case ZType::Crawler: speed *= 1.1f; hp *= 0.7f; z.scale = 0.9f; break;
    }
    if (round == 1) hp = 1.0f; // one hit at round 1
    z.health = z.maxHealth = hp;
    z.speed = speed * difficultySpeed() * (0.85f + 0.3f * rand01());

    // Spawn from the top or from the sides (upper half)
    const float side = rand01();
    if (side < 0.6f) { z.x = 30.0f + rand01() * (kWidth - 60.0f); z.y = -20.0f; }
    else if (side < 0.8f) { z.x = -25.0f; z.y = 40.0f + rand01() * 160.0f; }
    else { z.x = kWidth + 25.0f; z.y = 40.0f + rand01() * 160.0f; }
    z.tx = kBarricadeLeft + 18.0f + rand01() * (kBarricadeRight - kBarricadeLeft - 36.0f);
    z.ty = kBarricadeY - 4.0f;
    z.anim = rand01() * 6.0f;
    z.groanTimer = 1.0f + rand01() * 6.0f;
    z.tearTimer = 1.6f;
    zombies.push_back (z);
    ++spawned;
}

void GameWorld::award (int pts, float x, float y, uint32_t colour)
{
    if (doublePointsTime > 0.0f) pts *= 2;
    host.gameAddPoints (pts);
    texts.push_back ({ x, y, 1.1f, "+" + std::to_string (pts), colour, pts >= 100 ? 1.25f : 1.0f });
}

void GameWorld::emit (PType t, float x, float y, int count, float speed, float life, float size)
{
    if (lowGraphics && t != PType::Shell) return;
    if (lowGraphics) return;
    for (int i = 0; i < count && particles.size() < 600; ++i)
    {
        const float a = rand01() * 6.2831853f;
        const float s = speed * (0.3f + 0.7f * rand01());
        particles.push_back ({ t, x, y, std::cos (a) * s, std::sin (a) * s - speed * 0.3f, life * (0.6f + 0.4f * rand01()),
                               life, size * (0.6f + 0.6f * rand01()), rand01() * 6.28f, (rand01() - 0.5f) * 12.0f });
    }
}

void GameWorld::damageZombie (Zombie& z, float dmg, bool head)
{
    if (z.state == ZState::Dying) return;
    if (instaKillTime > 0.0f) dmg = 1.0e6f;
    z.health -= dmg;
    z.hitFlash = 0.12f;
    const float hy = head ? z.y - 38.0f * z.scale : z.y - 18.0f * z.scale;
    emit (PType::Ichor, z.x, hy, head ? 10 : 6, 70.0f, 0.5f, 2.2f);
    emit (PType::Dust, z.x, hy, 4, 30.0f, 0.7f, 4.0f);
    if (z.health <= 0.0f) killZombie (z, head);
    else
    {
        award (10, z.x, hy - 10.0f);
        host.gameSfx (Sfx::ZombieHit, 60, 0.6f, (z.x / kWidth) * 2.0f - 1.0f);
    }
}

void GameWorld::killZombie (Zombie& z, bool head)
{
    z.state = ZState::Dying;
    z.deathTimer = 0.6f;
    z.headshotDeath = head;
    ++kills;
    const float pan = (z.x / kWidth) * 2.0f - 1.0f;
    award (head ? 100 : 60, z.x, z.y - 50.0f * z.scale, head ? kGold : kGreen);
    if (head) host.gameSfx (Sfx::Headshot, 60, 0.6f, pan);
    host.gameSfx (Sfx::ZombieDeath, 50 + rand01() * 20, 0.55f, pan);
    emit (PType::Dust, z.x, z.y - 10.0f, 14, 50.0f, 1.0f, 6.0f);
    // 3% power-up drop
    if (rand01() < 0.03f)
    {
        const float r = rand01();
        const PowerUp t = r < 0.3f ? PowerUp::DoublePoints : r < 0.6f ? PowerUp::InstaKill : r < 0.88f ? PowerUp::MaxAmmo : PowerUp::Nuke;
        drops.push_back ({ t, std::clamp (z.x, 30.0f, kWidth - 30.0f), std::clamp (z.y - 20.0f, 40.0f, kBarricadeY - 30.0f), 15.0f });
        host.gameSfx (Sfx::PowerUpDrop, 76, 0.6f, pan);
    }
}

void GameWorld::shoot()
{
    if (paused || downTimer > 0.0f) return;
    if (reloadTimer > 0.0f || fireCooldown > 0.0f) return;
    if (ammo <= 0) { host.gameSfx (Sfx::DryFire, 60, 0.7f, 0); reload(); return; }
    --ammo;
    fireCooldown = 0.11f;
    muzzleFlash = 0.06f;
    recoil = 1.0f;
    if (! lowGraphics) shake = std::max (shake, 2.2f);
    host.gameSfx (Sfx::Gunshot, 60, 0.8f, (aimX / kWidth - 0.5f) * 0.6f);
    // Shell casing
    if (! lowGraphics)
        particles.push_back ({ PType::Shell, kPlayerX + 10.0f, kPlayerY - 22.0f, 60.0f + rand01() * 40.0f, -120.0f - rand01() * 40.0f,
                               1.2f, 1.2f, 3.0f, 0.0f, 18.0f });

    // Front-most (largest y) zombie under the crosshair takes the hit
    Zombie* best = nullptr; bool head = false;
    for (auto& z : zombies)
    {
        if (z.state == ZState::Dying) continue;
        bool hitHead = false, hitBody = false;
        if (z.type == ZType::Crawler)
        {
            const float dx = (aimX - z.x) / (18.0f * z.scale), dy = (aimY - (z.y - 7.0f)) / (8.0f * z.scale);
            hitBody = dx * dx + dy * dy <= 1.0f;
            const float hx = aimX - (z.x + 14.0f * z.scale), hy = aimY - (z.y - 10.0f * z.scale);
            hitHead = hx * hx + hy * hy <= 36.0f * z.scale * z.scale;
        }
        else
        {
            const float dx = (aimX - z.x) / (11.0f * z.scale), dy = (aimY - (z.y - 18.0f * z.scale)) / (17.0f * z.scale);
            hitBody = dx * dx + dy * dy <= 1.0f;
            const float hx = aimX - z.x, hy = aimY - (z.y - 40.0f * z.scale);
            hitHead = hx * hx + hy * hy <= 64.0f * z.scale * z.scale;
        }
        if ((hitHead || hitBody) && (best == nullptr || z.y > best->y)) { best = &z; head = hitHead; }
    }
    if (best != nullptr) damageZombie (*best, head ? 2.0f : 1.0f, head);
    else emit (PType::Spark, aimX, aimY, 5, 60.0f, 0.3f, 1.5f);
    if (ammo == 0) reload();
}

void GameWorld::midiShot (float velocity)
{
    if (paused || downTimer > 0.0f) return;
    Zombie* target = nullptr;
    float bestD = 1.0e9f;
    for (auto& z : zombies)
    {
        if (z.state == ZState::Dying || z.y < 0.0f) continue;
        const float d = std::hypot (z.x - kPlayerX, z.y - kPlayerY);
        if (d < bestD) { bestD = d; target = &z; }
    }
    muzzleFlash = 0.05f;
    recoil = 0.6f;
    host.gameSfx (Sfx::Gunshot, 60, 0.35f + 0.4f * velocity, 0);
    if (target == nullptr) return;
    aimX = target->x; aimY = target->y - 20.0f * target->scale;
    const bool head = velocity > 0.85f;
    damageZombie (*target, 0.5f + velocity * 1.5f, head);
}

void GameWorld::reload()
{
    if (reloadTimer > 0.0f || ammo == kMagSize) return;
    reloadTimer = 1.3f;
    host.gameSfx (Sfx::Reload, 60, 0.7f, 0.1f);
}

bool GameWorld::isPointOnBox (float x, float y) const noexcept
{
    return boxAlpha > 0.5f && std::fabs (x - boxX) < 52.0f && y > boxY - 34.0f && y < boxY + 30.0f;
}

bool GameWorld::isPointOnResult (float x, float y) const noexcept
{
    if (boxState != BoxState::Landed || resultIsMascot) return false;
    const float ry = resultCenterY() + 6.0f;
    return std::fabs (x - boxX) < 92.0f && std::fabs (y - ry) < 38.0f;
}

bool GameWorld::click (float x, float y)
{
    if (downTimer > 0.0f) return true;
    // Power-ups
    for (size_t i = 0; i < drops.size(); ++i)
    {
        if (std::hypot (drops[i].x - x, drops[i].y - y) < 20.0f)
        {
            const auto t = drops[i].type;
            drops.erase (drops.begin() + (long) i);
            host.gameSfx (Sfx::PowerUpGrab, t == PowerUp::Nuke ? 48 : 64, 0.8f, 0);
            switch (t)
            {
                case PowerUp::DoublePoints: doublePointsTime = 30.0f; texts.push_back ({ x, y, 1.6f, "DOUBLE POINTS", kGold, 1.3f }); break;
                case PowerUp::InstaKill:    instaKillTime = 30.0f;    texts.push_back ({ x, y, 1.6f, "INSTA-KILL", kRed, 1.3f }); break;
                case PowerUp::MaxAmmo:      ammo = kMagSize; reloadTimer = 0; texts.push_back ({ x, y, 1.6f, "MAX AMMO", kCyan, 1.3f }); break;
                case PowerUp::Nuke:
                {
                    host.gameSfx (Sfx::Nuke, 40, 1.0f, 0);
                    nukeFlash = 1.0f;
                    if (! lowGraphics) shake = 14.0f;
                    for (auto& z : zombies)
                        if (z.state != ZState::Dying) { z.state = ZState::Dying; z.deathTimer = 0.6f; ++kills; emit (PType::Ember, z.x, z.y - 20, 8, 60, 1.0f, 3.0f); }
                    award (400, x, y, kGold);
                    break;
                }
            }
            return true;
        }
    }
    if (isPointOnResult (x, y)) { takeResult(); return true; }
    if (isPointOnBox (x, y) && boxState == BoxState::Idle) { requestSpin(); return true; }
    return false;
}

void GameWorld::requestSpin()
{
    if (boxState != BoxState::Idle || paused) return;
    const int cost = getSpinCost();
    if (! host.gameSpendPoints (cost))
    {
        texts.push_back ({ boxX, boxY - 40.0f, 1.2f, "NEED " + std::to_string (cost), kRed, 1.0f });
        host.gameSfx (Sfx::DryFire, 60, 0.6f, 0);
        return;
    }
    const auto res = host.gameRollBox();
    resultIsMascot = res.mascot;
    resultPreset = res.presetId;
    if (resultIsMascot && cost > 0) host.gameAddPoints (cost); // refunded (not doubled)
    boxState = BoxState::Opening;
    boxTimer = 0.0f;
    cycleTimer = 0.0f;
    cycleInterval = 0.05f;
    chimeDegree = 0;
    host.gameSfx (Sfx::BoxCreak, 60, 0.8f, 0);
}

void GameWorld::takeResult()
{
    if (boxState != BoxState::Landed || resultIsMascot || resultPreset <= 0) return;
    host.gameTakeInstrument (resultPreset);
    host.gameSfx (Sfx::PowerUpGrab, 72, 0.7f, 0);
    boxState = BoxState::Taken;
    boxTimer = 0.0f;
}

void GameWorld::konamiEntered()
{
    host.gameKonami();
}

void GameWorld::playerDown()
{
    downTimer = 3.0f;
    host.gameSfx (Sfx::PlayerDown, 40, 0.9f, 0);
}

void GameWorld::update (float dt)
{
    lowGraphics = host.gameSettings().graphics == 0;
    dt = std::min (dt, 0.05f);
    if (konamiTimer > 0.0f) konamiTimer -= dt;
    // The box keeps animating while paused so a spin in progress can still be taken.
    updateBox (dt);
    if (paused) return;
    time += dt;

    if (downTimer > 0.0f)
    {
        downTimer -= dt;
        if (downTimer <= 0.0f)
        {
            reset (false);
            texts.push_back ({ kWidth * 0.5f, 220.0f, 2.0f, "BACK ON YOUR FEET - POINTS KEPT", kGreen, 1.1f });
        }
        updateParticles (dt);
        return;
    }

    fireCooldown = std::max (0.0f, fireCooldown - dt);
    muzzleFlash = std::max (0.0f, muzzleFlash - dt);
    recoil = std::max (0.0f, recoil - dt * 8.0f);
    shake = std::max (0.0f, shake - dt * 30.0f);
    nukeFlash = std::max (0.0f, nukeFlash - dt * 1.5f);
    roundBanner = std::max (0.0f, roundBanner - dt);
    doublePointsTime = std::max (0.0f, doublePointsTime - dt);
    instaKillTime = std::max (0.0f, instaKillTime - dt);
    if (reloadTimer > 0.0f) { reloadTimer -= dt; if (reloadTimer <= 0.0f) { reloadTimer = 0.0f; ammo = kMagSize; } }

    // Rebuild boards while E is held
    if (rebuildHeld && boards < kMaxBoards)
    {
        rebuildTimer += dt;
        if (rebuildTimer >= 0.8f)
        {
            rebuildTimer = 0.0f;
            ++boards;
            host.gameSfx (Sfx::BoardRebuild, 60, 0.7f, 0);
            award (10, kPlayerX, kBarricadeY - 20.0f);
            emit (PType::Dust, kPlayerX + (rand01() - 0.5f) * 150.0f, kBarricadeY, 6, 30.0f, 0.6f, 4.0f);
        }
    }
    else rebuildTimer = 0.0f;

    // Rounds
    if (roundState == RoundState::Break)
    {
        roundTimer -= dt;
        if (roundTimer <= 0.0f) startRound();
    }
    else
    {
        const int maxAlive = std::min (24, 8 + round);
        int alive = 0;
        for (auto& z : zombies) alive += z.state != ZState::Dying ? 1 : 0;
        spawnTimer -= dt;
        if (spawned < toSpawn && alive < maxAlive && spawnTimer <= 0.0f)
        {
            spawnZombie();
            spawnTimer = std::max (0.45f, 2.0f - 0.08f * (float) round) * (0.7f + 0.6f * rand01());
        }
        if (spawned >= toSpawn && alive == 0)
        {
            award (50 * round, kWidth * 0.5f, 120.0f, kGold);
            host.gameSfx (Sfx::RoundEnd, 60, 0.7f, 0);
            ++round;
            roundState = RoundState::Break;
            roundTimer = 10.0f;
            roundBanner = 2.5f;
        }
    }

    updateZombies (dt);

    for (auto& d : drops) d.life -= dt;
    drops.erase (std::remove_if (drops.begin(), drops.end(), [] (const Drop& d) { return d.life <= 0.0f; }), drops.end());
    updateParticles (dt);
}

void GameWorld::updateZombies (float dt)
{
    for (auto& z : zombies)
    {
        z.anim += dt * (z.type == ZType::Runner ? 9.0f : 4.0f);
        z.hitFlash = std::max (0.0f, z.hitFlash - dt);
        if (z.state == ZState::Dying) { z.deathTimer -= dt; continue; }

        z.groanTimer -= dt;
        if (z.groanTimer <= 0.0f)
        {
            z.groanTimer = 4.0f + rand01() * 8.0f;
            if (rand01() < 0.5f) host.gameSfx (Sfx::ZombieGroan, 48 + rand01() * 24, 0.35f, (z.x / kWidth) * 2.0f - 1.0f);
        }

        if (z.state == ZState::Approach)
        {
            const float dx = z.tx - z.x, dy = z.ty - z.y;
            const float d = std::hypot (dx, dy);
            if (d < 3.0f) { z.state = ZState::AtBarricade; z.tearTimer = 1.6f; }
            else
            {
                z.sway = std::sin (z.anim * 0.7f) * 8.0f;
                z.x += (dx / d) * z.speed * dt + std::cos (z.anim * 0.7f) * 0.15f;
                z.y += (dy / d) * z.speed * dt;
            }
        }
        else if (z.state == ZState::AtBarricade)
        {
            if (boards > 0)
            {
                z.tearTimer -= dt * (z.type == ZType::Brute ? 1.8f : 1.0f) * difficultySpeed();
                if (z.tearTimer <= 0.0f)
                {
                    z.tearTimer = 1.6f;
                    --boards;
                    host.gameSfx (Sfx::BoardRip, 60, 0.7f, (z.x / kWidth) * 2.0f - 1.0f);
                    emit (PType::Dust, z.x, kBarricadeY, 8, 40.0f, 0.8f, 5.0f);
                }
            }
            else z.state = ZState::Inside;
        }
        else // Inside: lunge at the player
        {
            const float dx = kPlayerX - z.x, dy = kPlayerY - z.y;
            const float d = std::hypot (dx, dy);
            if (d < 20.0f) { playerDown(); return; }
            z.x += (dx / d) * z.speed * 1.3f * dt;
            z.y += (dy / d) * z.speed * 1.3f * dt;
        }
    }
    zombies.erase (std::remove_if (zombies.begin(), zombies.end(), [] (const Zombie& z) { return z.state == ZState::Dying && z.deathTimer <= 0.0f; }), zombies.end());
    // Painter's order: draw back-to-front
    std::sort (zombies.begin(), zombies.end(), [] (const Zombie& a, const Zombie& b) { return a.y < b.y; });
}

void GameWorld::updateParticles (float dt)
{
    for (auto& p : particles)
    {
        p.life -= dt;
        p.x += p.vx * dt; p.y += p.vy * dt;
        p.rot += p.vrot * dt;
        switch (p.type)
        {
            case PType::Shell: p.vy += 600.0f * dt; if (p.y > kPlayerY + 12.0f && p.vy > 0) { p.vy *= -0.35f; p.vx *= 0.5f; p.vrot *= 0.5f; } break;
            case PType::Ichor: p.vy += 300.0f * dt; break;
            case PType::Dust: case PType::Chalk: p.vx *= 0.96f; p.vy = p.vy * 0.96f - 8.0f * dt; break;
            case PType::Ember: p.vy -= 30.0f * dt; break;
            case PType::Spark: p.vx *= 0.9f; p.vy *= 0.9f; break;
        }
    }
    particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }), particles.end());
    for (auto& t : texts) { t.life -= dt; t.y -= 28.0f * dt; }
    texts.erase (std::remove_if (texts.begin(), texts.end(), [] (const FloatText& t) { return t.life <= 0.0f; }), texts.end());
}

void GameWorld::updateBox (float dt)
{
    boxTimer += dt;
    switch (boxState)
    {
        case BoxState::Idle:
            lidAngle = std::max (0.0f, lidAngle - dt * 3.0f);
            break;
        case BoxState::Opening:
            lidAngle = std::min (1.0f, boxTimer / 0.5f);
            if (boxTimer >= 0.5f) { boxState = BoxState::Spinning; boxTimer = 0.0f; displayPreset = host.gameDisplayPreset (cycleSeed++); }
            break;
        case BoxState::Spinning:
        {
            // Icons cycle fast at first and decelerate (ease-out); every tick is a key-pitched chime.
            const float t = boxTimer / kSpinDuration;
            cycleInterval = 0.045f + 0.42f * easeOutCubic (t) * easeOutCubic (t);
            cycleTimer += dt;
            if (cycleTimer >= cycleInterval && t < 0.97f)
            {
                cycleTimer = 0.0f;
                displayPreset = host.gameDisplayPreset (cycleSeed++ * 2654435761u);
                host.gameSfx (Sfx::BoxChime, (float) host.gameChimeNote (chimeDegree++), 0.45f, (rand01() - 0.5f) * 0.6f);
                if (! lowGraphics) emit (PType::Ember, boxX + (rand01() - 0.5f) * 60.0f, boxY - 20.0f, 1, 20.0f, 1.2f, 2.0f);
            }
            if (boxTimer >= kSpinDuration)
            {
                boxTimer = 0.0f;
                if (resultIsMascot)
                {
                    boxState = BoxState::Mascot;
                    host.gameSfx (Sfx::MascotLaugh, 60, 0.9f, 0);
                    texts.push_back ({ boxX, boxY - 90.0f, 2.2f, "BYE BYE! (points refunded)", kCyan, 1.0f });
                }
                else
                {
                    boxState = BoxState::Landed;
                    displayPreset = resultPreset;
                    const int rarity = host.gamePresetRarity (resultPreset);
                    host.gameSfx (rarity == (int) Rarity::Wonder ? Sfx::WonderRay : Sfx::BoxReveal,
                                  (float) host.gameChimeNote (0), 0.8f, 0);
                    host.gameBoxLanded (resultPreset);
                    if (! lowGraphics) emit (rarity >= 3 ? PType::Ember : PType::Spark, boxX, boxY - 70.0f, 24, 90.0f, 1.0f, 2.5f);
                }
            }
            break;
        }
        case BoxState::Landed:
            resultHover = boxTimer;
            if (boxTimer >= kResultTimeout)
            {
                host.gameLostInstrument (resultPreset);
                texts.push_back ({ boxX, boxY - 60.0f, 1.5f, "LOST TO THE BOX...", kRed, 1.0f });
                boxState = BoxState::Sinking;
                boxTimer = 0.0f;
            }
            break;
        case BoxState::Taken:
        case BoxState::Sinking:
            if (boxTimer >= 0.6f) { boxState = BoxState::Idle; boxTimer = 0.0f; }
            break;
        case BoxState::Mascot:
            // The box rises and fades away, then reappears somewhere else
            boxAlpha = std::max (0.0f, 1.0f - std::max (0.0f, boxTimer - 1.2f) / 0.8f);
            if (boxTimer >= 2.4f)
            {
                boxNewX = 110.0f + rand01() * (kWidth - 220.0f);
                if (std::fabs (boxNewX - boxX) < 120.0f) boxNewX = boxX > kWidth * 0.5f ? boxX - 220.0f : boxX + 220.0f;
                boxNewY = 60.0f + rand01() * 90.0f;
                host.gameSfx (Sfx::BoxTeleport, 60, 0.8f, (boxNewX / kWidth) * 2.0f - 1.0f);
                boxX = boxNewX; boxY = boxNewY;
                boxState = BoxState::Teleport;
                boxTimer = 0.0f;
            }
            break;
        case BoxState::Teleport:
            boxAlpha = std::min (1.0f, boxTimer / 1.0f);
            lidAngle = 0.0f;
            if (boxTimer >= 1.0f) { boxAlpha = 1.0f; boxState = BoxState::Idle; boxTimer = 0.0f; }
            break;
    }
}

} // namespace mbx
