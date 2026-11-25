// Mystery Box Synthesizer by Raygun
// "SURVIVE TO SPIN" -- the mini-game simulation. Pure logic, no drawing: the
// GameView renders this state. Runs on the message thread only and never
// touches the audio thread (sounds go through GameHost -> lock-free FIFO).
#pragma once

#include "../engine/SfxEngine.h"
#include "../presets/MysteryBoxRoller.h"
#include "../util/PersistentStore.h"
#include <string>
#include <vector>

namespace mbx
{
class GameHost
{
public:
    virtual ~GameHost() = default;
    virtual void gameAddPoints (int pts) = 0;
    virtual int gamePoints() = 0;
    virtual bool gameSpendPoints (int pts) = 0;
    virtual void gameSfx (Sfx type, float note, float gain, float pan) = 0;
    virtual int gameChimeNote (int degree) = 0;
    virtual MysteryBoxRoller::Result gameRollBox() = 0;
    virtual void gameBoxLanded (int presetId) = 0;
    virtual void gameTakeInstrument (int presetId) = 0;
    virtual void gameLostInstrument (int presetId) = 0;
    virtual int gameDisplayPreset (uint32_t seed) = 0;
    virtual const char* gamePresetName (int presetId) = 0;
    virtual int gamePresetCategory (int presetId) = 0;
    virtual int gamePresetRarity (int presetId) = 0;
    virtual const UserSettings& gameSettings() = 0;
    virtual void gameKonami() = 0;
};

class GameWorld
{
public:
    static constexpr float kWidth = 720.0f, kHeight = 440.0f;
    static constexpr int kMaxBoards = 6;
    static constexpr int kMagSize = 8;
    static constexpr float kBarricadeY = 338.0f, kBarricadeLeft = 262.0f, kBarricadeRight = 458.0f;
    static constexpr float kPlayerX = 360.0f, kPlayerY = 408.0f;

    enum class ZType { Walker, Runner, Brute, Crawler };
    enum class ZState { Approach, AtBarricade, Inside, Dying };
    struct Zombie
    {
        ZType type = ZType::Walker;
        ZState state = ZState::Approach;
        float x = 0, y = 0, tx = 0, ty = 0;
        float speed = 20, health = 1, maxHealth = 1, scale = 1;
        float anim = 0, tearTimer = 0, hitFlash = 0, deathTimer = 0, groanTimer = 3;
        float sway = 0;
        bool headshotDeath = false;
        uint32_t seed = 0;
    };
    enum class PType { Dust, Ichor, Shell, Spark, Ember, Chalk };
    struct Particle { PType type; float x, y, vx, vy, life, maxLife, size, rot, vrot; };
    struct FloatText { float x, y, life; std::string text; uint32_t colour; float scale; };
    enum class PowerUp { DoublePoints, InstaKill, MaxAmmo, Nuke };
    struct Drop { PowerUp type; float x, y, life; };

    enum class RoundState { Break, Active };
    enum class BoxState { Idle, Opening, Spinning, Landed, Taken, Sinking, Mascot, Teleport };

    explicit GameWorld (GameHost& host);

    void update (float dt);
    void reset (bool keepRound = false);

    // Input (game coordinates)
    void aim (float x, float y) noexcept { aimX = x; aimY = y; }
    void shoot();
    void midiShot (float velocity);
    void reload();
    void setRebuilding (bool held) noexcept { rebuildHeld = held; }
    void togglePause() noexcept { paused = ! paused; }
    void setPaused (bool p) noexcept { paused = p; }
    bool isPaused() const noexcept { return paused; }
    /** Returns true if the click was consumed by the box / result / power-up. */
    bool click (float x, float y);
    void requestSpin();
    void takeResult();
    void konamiEntered();

    bool isPointOnBox (float x, float y) const noexcept;
    bool isPointOnResult (float x, float y) const noexcept;
    /** The won instrument floats just below the box (always fully on screen). */
    float resultCenterY() const noexcept { return boxY + 80.0f; }
    int getSpinCost() const;
    bool canAffordSpin() const;

    // State for rendering
    std::vector<Zombie> zombies;
    std::vector<Particle> particles;
    std::vector<FloatText> texts;
    std::vector<Drop> drops;
    int round = 1, boards = kMaxBoards, ammo = kMagSize, kills = 0;
    RoundState roundState = RoundState::Break;
    float roundTimer = 3.0f, roundBanner = 0.0f;
    float reloadTimer = 0.0f, fireCooldown = 0.0f, muzzleFlash = 0.0f, recoil = 0.0f, shake = 0.0f;
    float doublePointsTime = 0.0f, instaKillTime = 0.0f, nukeFlash = 0.0f;
    float downTimer = 0.0f;
    float aimX = 360.0f, aimY = 200.0f;
    float time = 0.0f;
    float rebuildTimer = 0.0f;
    bool rebuildHeld = false;

    // Mystery box
    BoxState boxState = BoxState::Idle;
    float boxX = 360.0f, boxY = 74.0f, boxTimer = 0.0f, lidAngle = 0.0f, boxAlpha = 1.0f;
    float boxNewX = 360.0f, boxNewY = 74.0f;
    int displayPreset = 0;       // the instrument currently shown above the box
    int resultPreset = 0;        // the final result
    bool resultIsMascot = false;
    float cycleTimer = 0.0f, cycleInterval = 0.05f;
    int chimeDegree = 0;
    uint32_t cycleSeed = 1;
    float resultHover = 0.0f;
    std::string konamiMessage;
    float konamiTimer = 0.0f;
    int lastKillStreak = 0;

    // Settings derived per update
    bool lowGraphics = false;

private:
    void startRound();
    void spawnZombie();
    void updateZombies (float dt);
    void updateBox (float dt);
    void updateParticles (float dt);
    void damageZombie (Zombie& z, float dmg, bool head);
    void killZombie (Zombie& z, bool head);
    void award (int pts, float x, float y, uint32_t colour = 0xff39ff14);
    void emit (PType t, float x, float y, int count, float speed, float life, float size);
    void playerDown();
    float rand01() noexcept;
    float difficultyHealth() const;
    float difficultySpeed() const;

    GameHost& host;
    bool paused = false;
    int toSpawn = 0, spawned = 0;
    float spawnTimer = 0.0f;
    uint32_t rngState = 0x1234567u;
};

} // namespace mbx
