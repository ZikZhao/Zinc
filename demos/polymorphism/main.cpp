#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct Hitbox {
    int x;
    int y;
    int w;
    int h;
};

static bool overlaps(const Hitbox& a, const Hitbox& b) {
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
}

class World;
class Entity;

class ITickable {
public:
    virtual ~ITickable() = default;
    virtual void tick(float deltaTime, World& world) = 0;
};

class IRenderable {
public:
    virtual ~IRenderable() = default;
    virtual void draw(World& world) const = 0;
};

class ICollidable {
public:
    virtual ~ICollidable() = default;
    virtual Hitbox getHitbox() const = 0;
    virtual void onCollide(ICollidable& other, World& world) = 0;
    virtual Entity* asEntity() = 0;
    virtual const Entity* asEntity() const = 0;
};

class IDamageable {
public:
    virtual ~IDamageable() = default;
    virtual void takeDamage(int amount) = 0;
    virtual bool isDead() const = 0;
};

class IInteractable {
public:
    virtual ~IInteractable() = default;
    virtual void interact(Entity& actor, World& world) = 0;
};

class Entity {
public:
    Entity(std::string name, int x, int y, char glyph)
        : name_(std::move(name)), x_(x), y_(y), glyph_(glyph) {}

    virtual ~Entity() = default;

    const std::string& name() const { return name_; }
    int x() const { return x_; }
    int y() const { return y_; }
    char glyph() const { return glyph_; }
    bool pendingDestroy() const { return pendingDestroy_; }

    void setPosition(int nx, int ny) {
        x_ = nx;
        y_ = ny;
    }

    void markForDestroy() { pendingDestroy_ = true; }

protected:
    std::string name_;
    int x_;
    int y_;
    char glyph_;
    bool pendingDestroy_ = false;
};

class World {
public:
    World(int width, int height)
        : width_(width), height_(height), canvas_(height, std::string(width, '.')) {}

    template <typename T, typename... Args>
    T& spawn(Args&&... args) {
        auto instance = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = instance.get();
        entities_.push_back(std::move(instance));
        return *raw;
    }

    void rebuildViews() {
        tickables_.clear();
        renderables_.clear();
        collidables_.clear();
        damageables_.clear();
        interactables_.clear();

        for (auto& uptr : entities_) {
            if (uptr->pendingDestroy()) {
                continue;
            }
            Entity* e = uptr.get();
            if (auto* v = dynamic_cast<ITickable*>(e)) tickables_.push_back(v);
            if (auto* v = dynamic_cast<IRenderable*>(e)) renderables_.push_back(v);
            if (auto* v = dynamic_cast<ICollidable*>(e)) collidables_.push_back(v);
            if (auto* v = dynamic_cast<IDamageable*>(e)) damageables_.push_back(v);
            if (auto* v = dynamic_cast<IInteractable*>(e)) interactables_.push_back(v);
        }
    }

    void tickAll(float dt) {
        for (auto* tickable : tickables_) {
            tickable->tick(dt, *this);
        }
    }

    void resolveCollisions() {
        for (std::size_t i = 0; i < collidables_.size(); ++i) {
            for (std::size_t j = i + 1; j < collidables_.size(); ++j) {
                ICollidable* a = collidables_[i];
                ICollidable* b = collidables_[j];
                Entity* ea = a->asEntity();
                Entity* eb = b->asEntity();
                if (!ea || !eb || ea->pendingDestroy() || eb->pendingDestroy()) {
                    continue;
                }
                if (overlaps(a->getHitbox(), b->getHitbox())) {
                    a->onCollide(*b, *this);
                    b->onCollide(*a, *this);
                }
            }
        }
    }

    void drawAll() {
        clearCanvas();
        for (const auto* renderable : renderables_) {
            renderable->draw(*this);
        }
    }

    void cleanupDestroyed() {
        entities_.erase(
            std::remove_if(
                entities_.begin(),
                entities_.end(),
                [](const std::unique_ptr<Entity>& e) { return e->pendingDestroy(); }
            ),
            entities_.end()
        );
    }

    void putPixel(int x, int y, char c) {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return;
        }
        canvas_[y][x] = c;
    }

    void drawRect(const Hitbox& b, char c) {
        for (int dy = 0; dy < b.h; ++dy) {
            for (int dx = 0; dx < b.w; ++dx) {
                putPixel(b.x + dx, b.y + dy, c);
            }
        }
    }

    void printFrame(long long frameIndex, int targetFps) const {
        std::cout << "\x1b[H";
        std::cout << "==== Zinc Polymorphism Sandbox ====\n";
        std::cout << "Frame=" << frameIndex << " TargetFPS=" << targetFps << " (Ctrl+C to stop)\n";
        for (const auto& line : canvas_) {
            std::cout << line << '\n';
        }
        std::cout << "Entities=" << livingEntityCount() << " Tickables=" << tickables_.size()
                  << " Renderables=" << renderables_.size()
                  << " Collidables=" << collidables_.size()
                  << " Damageables=" << damageables_.size()
                  << " Interactables=" << interactables_.size() << '\n';

        std::cout << "Legend: . empty  P player  Z zombie  S slime  G ghost  # wall  = dirt\n";
        std::cout
            << "        ~/- water  L/! lava  ^/v trap  D door(closed)  l/L lever  b/B button\n";
        std::cout << "        H potion  $ coin  * damaged-flash\n";
        std::cout.flush();
    }

    int width() const { return width_; }
    int height() const { return height_; }

    std::vector<IInteractable*>& interactables() { return interactables_; }
    const std::vector<std::unique_ptr<Entity>>& entities() const { return entities_; }

    std::optional<Entity*> findByName(const std::string& needle) {
        for (auto& e : entities_) {
            if (!e->pendingDestroy() && e->name() == needle) {
                return e.get();
            }
        }
        return std::nullopt;
    }

private:
    void clearCanvas() {
        for (auto& line : canvas_) {
            std::fill(line.begin(), line.end(), '.');
        }
    }

    std::size_t livingEntityCount() const {
        std::size_t count = 0;
        for (const auto& e : entities_) {
            if (!e->pendingDestroy()) {
                ++count;
            }
        }
        return count;
    }

    int width_;
    int height_;
    std::vector<std::string> canvas_;
    std::vector<std::unique_ptr<Entity>> entities_;
    std::vector<ITickable*> tickables_;
    std::vector<IRenderable*> renderables_;
    std::vector<ICollidable*> collidables_;
    std::vector<IDamageable*> damageables_;
    std::vector<IInteractable*> interactables_;
};

class TerrainEntity : public Entity, public IRenderable, public ICollidable {
public:
    TerrainEntity(std::string name, int x, int y, char glyph)
        : Entity(std::move(name), x, y, glyph) {}

    Hitbox getHitbox() const override { return Hitbox{x_, y_, 1, 1}; }
    void onCollide(ICollidable&, World&) override {}
    Entity* asEntity() override { return this; }
    const Entity* asEntity() const override { return this; }
    void draw(World& world) const override { world.drawRect(getHitbox(), glyph_); }
};

class StoneWall : public TerrainEntity {
public:
    StoneWall(int x, int y) : TerrainEntity("StoneWall", x, y, '#') {}
};

class DirtBlock : public TerrainEntity {
public:
    DirtBlock(int x, int y) : TerrainEntity("DirtBlock", x, y, '=') {}
};

class Water : public Entity, public ITickable, public IRenderable, public ICollidable {
public:
    Water(int x, int y) : Entity("Water", x, y, '~') {}

    void tick(float, World& world) override {
        flowPhase_ = (flowPhase_ + 1) % 2;
        if (x_ + 1 < world.width() && flowPhase_ == 1) {
            x_ += 1;
        }
    }

    Hitbox getHitbox() const override { return Hitbox{x_, y_, 1, 1}; }
    void onCollide(ICollidable&, World&) override {}
    Entity* asEntity() override { return this; }
    const Entity* asEntity() const override { return this; }
    void draw(World& world) const override {
        world.drawRect(getHitbox(), flowPhase_ == 0 ? '~' : '-');
    }

private:
    int flowPhase_ = 0;
};

class Lava : public Entity,
             public ITickable,
             public IRenderable,
             public ICollidable,
             public IDamageable {
public:
    Lava(int x, int y) : Entity("Lava", x, y, 'L') {}

    void tick(float, World&) override { pulse_ = (pulse_ + 1) % 3; }
    Hitbox getHitbox() const override { return Hitbox{x_, y_, 1, 1}; }

    void onCollide(ICollidable& other, World&) override {
        if (auto* target = dynamic_cast<IDamageable*>(&other)) {
            target->takeDamage(2);
        }
    }

    Entity* asEntity() override { return this; }
    const Entity* asEntity() const override { return this; }
    void draw(World& world) const override { world.drawRect(getHitbox(), pulse_ == 0 ? 'L' : '!'); }

    void takeDamage(int amount) override {
        hp_ -= amount;
        if (hp_ <= 0) {
            markForDestroy();
        }
    }

    bool isDead() const override { return hp_ <= 0; }

private:
    int hp_ = 8;
    int pulse_ = 0;
};

class BackgroundCloud : public Entity, public ITickable, public IRenderable {
public:
    BackgroundCloud(int x, int y) : Entity("BackgroundCloud", x, y, 'c') {}

    void tick(float, World& world) override { x_ = (x_ + 1) % world.width(); }

    void draw(World& world) const override { world.putPixel(x_, y_, glyph_); }
};

class CharacterBase : public Entity,
                      public ITickable,
                      public IRenderable,
                      public ICollidable,
                      public IDamageable,
                      public IInteractable {
public:
    CharacterBase(std::string name, int x, int y, char glyph, int hp)
        : Entity(std::move(name), x, y, glyph), hp_(hp) {}

    Hitbox getHitbox() const override { return Hitbox{x_, y_, 1, 1}; }
    Entity* asEntity() override { return this; }
    const Entity* asEntity() const override { return this; }

    void onCollide(ICollidable&, World&) override {}

    void takeDamage(int amount) override {
        hp_ -= amount;
        flashFrames_ = 1;
        if (hp_ <= 0) {
            markForDestroy();
        }
    }

    bool isDead() const override { return hp_ <= 0; }

    void interact(Entity&, World&) override {}

    void draw(World& world) const override {
        world.drawRect(getHitbox(), flashFrames_ > 0 ? '*' : glyph_);
    }

    void decayFlash() {
        if (flashFrames_ > 0) {
            --flashFrames_;
        }
    }

    int hp() const { return hp_; }

private:
    int hp_;
    int flashFrames_ = 0;
};

class Player : public CharacterBase {
public:
    Player(int x, int y) : CharacterBase("Player", x, y, 'P', 25) {}

    void tick(float, World& world) override {
        if (direction_ > 0) {
            if (x() + 1 < world.width()) {
                setPosition(x() + 1, y());
            } else {
                direction_ = -1;
            }
        } else {
            if (x() - 1 >= 0) {
                setPosition(x() - 1, y());
            } else {
                direction_ = 1;
            }
        }
        decayFlash();
    }

    void interact(Entity& actor, World&) override {
        std::cout << "Player acknowledged interaction from " << actor.name() << '\n';
    }

private:
    int direction_ = 1;
};

class Zombie : public CharacterBase {
public:
    Zombie(int x, int y) : CharacterBase("Zombie", x, y, 'Z', 14) {}

    void tick(float, World& world) override {
        auto playerPtr = world.findByName("Player");
        if (playerPtr.has_value()) {
            Entity* p = *playerPtr;
            int nx = x();
            if (p->x() > x()) nx += 1;
            if (p->x() < x()) nx -= 1;
            nx = std::clamp(nx, 0, world.width() - 1);
            setPosition(nx, y());
        }
        decayFlash();
    }

    void onCollide(ICollidable& other, World&) override {
        if (auto* d = dynamic_cast<IDamageable*>(&other)) {
            d->takeDamage(1);
        }
    }
};

class Slime : public CharacterBase {
public:
    Slime(int x, int y) : CharacterBase("Slime", x, y, 'S', 10) {}

    void tick(float, World& world) override {
        hop_ = !hop_;
        if (hop_) {
            int nx = std::clamp(x() + 1, 0, world.width() - 1);
            setPosition(nx, y());
        }
        decayFlash();
    }

private:
    bool hop_ = false;
};

class IndestructibleGhost : public Entity,
                            public ITickable,
                            public IRenderable,
                            public IInteractable {
public:
    IndestructibleGhost(int x, int y) : Entity("IndestructibleGhost", x, y, 'G') {}

    void tick(float, World& world) override { y_ = (y_ + 1) % world.height(); }

    void draw(World& world) const override { world.putPixel(x_, y_, glyph_); }

    void interact(Entity& actor, World&) override {
        std::cout << "Ghost whispers to " << actor.name() << '\n';
    }
};

class HealthPotion : public Entity, public IRenderable, public ICollidable {
public:
    HealthPotion(int x, int y) : Entity("HealthPotion", x, y, 'H') {}

    Hitbox getHitbox() const override { return Hitbox{x_, y_, 1, 1}; }

    void onCollide(ICollidable& other, World&) override {
        if (auto* d = dynamic_cast<IDamageable*>(&other)) {
            d->takeDamage(-5);
            markForDestroy();
        }
    }

    Entity* asEntity() override { return this; }
    const Entity* asEntity() const override { return this; }
    void draw(World& world) const override { world.drawRect(getHitbox(), glyph_); }
};

class GoldCoin : public Entity, public IRenderable, public ICollidable {
public:
    GoldCoin(int x, int y) : Entity("GoldCoin", x, y, '$') {}

    Hitbox getHitbox() const override { return Hitbox{x_, y_, 1, 1}; }

    void onCollide(ICollidable& other, World&) override {
        if (dynamic_cast<Player*>(&other)) {
            markForDestroy();
            std::cout << "GoldCoin picked up\n";
        }
    }

    Entity* asEntity() override { return this; }
    const Entity* asEntity() const override { return this; }
    void draw(World& world) const override { world.drawRect(getHitbox(), glyph_); }
};

class StoneDoor : public TerrainEntity {
public:
    StoneDoor(int x, int y) : TerrainEntity("StoneDoor", x, y, 'D') {}

    void setOpen(bool open) {
        open_ = open;
        glyph_ = open_ ? '.' : 'D';
    }

    bool open() const { return open_; }

private:
    bool open_ = false;
};

class Lever : public Entity, public IRenderable, public IInteractable {
public:
    Lever(int x, int y, StoneDoor* door) : Entity("Lever", x, y, 'l'), linkedDoor_(door) {}

    void draw(World& world) const override { world.putPixel(x_, y_, glyph_); }

    void interact(Entity& actor, World&) override {
        toggled_ = !toggled_;
        glyph_ = toggled_ ? 'L' : 'l';
        if (linkedDoor_ != nullptr) {
            linkedDoor_->setOpen(toggled_);
        }
        std::cout << actor.name() << " toggled Lever => " << (toggled_ ? "ON" : "OFF") << '\n';
    }

private:
    bool toggled_ = false;
    StoneDoor* linkedDoor_ = nullptr;
};

class Button : public Entity, public IRenderable, public IInteractable {
public:
    Button(int x, int y) : Entity("Button", x, y, 'b') {}

    void draw(World& world) const override { world.putPixel(x_, y_, glyph_); }

    void interact(Entity& actor, World&) override {
        pressed_ = !pressed_;
        glyph_ = pressed_ ? 'B' : 'b';
        std::cout << actor.name() << " pressed Button => " << (pressed_ ? "ON" : "OFF") << '\n';
    }

private:
    bool pressed_ = false;
};

class SpikeTrap : public Entity,
                  public ITickable,
                  public IRenderable,
                  public ICollidable,
                  public IDamageable {
public:
    SpikeTrap(int x, int y) : Entity("SpikeTrap", x, y, '^') {}

    void tick(float, World&) override {
        armed_ = !armed_;
        glyph_ = armed_ ? '^' : 'v';
    }

    Hitbox getHitbox() const override { return Hitbox{x_, y_, 1, 1}; }

    void onCollide(ICollidable& other, World&) override {
        if (!armed_) {
            return;
        }
        if (auto* victim = dynamic_cast<IDamageable*>(&other)) {
            victim->takeDamage(3);
        }
    }

    Entity* asEntity() override { return this; }
    const Entity* asEntity() const override { return this; }
    void draw(World& world) const override { world.drawRect(getHitbox(), glyph_); }

    void takeDamage(int amount) override {
        hp_ -= amount;
        if (hp_ <= 0) {
            markForDestroy();
        }
    }

    bool isDead() const override { return hp_ <= 0; }

private:
    bool armed_ = true;
    int hp_ = 12;
};

// Empty-state entity to emulate the "ZST-style" edge case in interface containers.
class InvisibleWall : public ICollidable {
public:
    InvisibleWall() = default;

    Hitbox getHitbox() const override { return Hitbox{0, 0, 1, 1}; }
    void onCollide(ICollidable&, World&) override {}
    Entity* asEntity() override { return nullptr; }
    const Entity* asEntity() const override { return nullptr; }
};

int main() {
    World world(40, 12);

    auto& door = world.spawn<StoneDoor>(31, 6);

    world.spawn<StoneWall>(30, 6);
    world.spawn<StoneWall>(32, 6);
    world.spawn<DirtBlock>(5, 10);
    world.spawn<DirtBlock>(6, 10);
    world.spawn<Water>(7, 8);
    world.spawn<Lava>(11, 8);
    world.spawn<BackgroundCloud>(2, 1);

    auto& player = world.spawn<Player>(3, 6);
    world.spawn<Zombie>(20, 6);
    world.spawn<Slime>(15, 6);
    world.spawn<IndestructibleGhost>(25, 2);

    world.spawn<HealthPotion>(10, 6);
    world.spawn<GoldCoin>(12, 6);
    world.spawn<Lever>(28, 6, &door);
    world.spawn<Button>(26, 6);
    world.spawn<SpikeTrap>(18, 6);

    InvisibleWall zstLikeCollidable;
    (void)zstLikeCollidable;

    constexpr int targetFps = 30;
    constexpr bool runForever = true;
    constexpr long long totalFrames = 2000;
    const auto frameBudget = std::chrono::milliseconds(1000 / targetFps);

    std::cout << "\x1b[2J\x1b[H";

    for (long long frame = 1; runForever || frame <= totalFrames; ++frame) {
        const auto frameStart = std::chrono::steady_clock::now();

        world.rebuildViews();
        world.tickAll(1.0f / 60.0f);
        world.resolveCollisions();

        if (frame == 4 || frame == 12) {
            world.rebuildViews();
            for (auto* target : world.interactables()) {
                if (target != nullptr) {
                    target->interact(player, world);
                }
            }
        }

        world.cleanupDestroyed();
        world.rebuildViews();
        world.drawAll();

        world.printFrame(frame, targetFps);

        auto playerEntity = world.findByName("Player");
        if (!playerEntity.has_value()) {
            std::cout << "Player died. Simulation ends early.\n";
            break;
        }

        const auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (elapsed < frameBudget) {
            std::this_thread::sleep_for(frameBudget - elapsed);
        }
    }

    std::cout << "\nSimulation complete.\n";
    return 0;
}
