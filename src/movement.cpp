#include "mega/world.hpp"
#include <limits>
#include <numeric>
namespace mega {
static std::uint64_t obstacleKey(int floor, int x, int z) {
  return (std::uint64_t(floor) << 26) | (std::uint64_t(x + 4096) << 13) | std::uint64_t(z + 4096);
}
float World::walkingSpeed(Id id, bool running) const {
  const auto &r = residents.at(id);
  float training = std::clamp(r.fitness, 0.f, 100.f) / 100.f;
  float condition = std::clamp(r.health / 50.f, .45f, 1.f);
  bool sprint = running && r.energy > .12f && r.thirst < 1.1f;
  return (sprint ? 7.2f : 3.6f) * (1 + training * .25f) * condition;
}
void World::exercise(Id id, double seconds, float intensity) {
  auto &r = residents.at(id);
  if (!r.alive || !r.present || !std::isfinite(seconds) || seconds <= 0 ||
      !std::isfinite(intensity) || intensity <= 0)
    return;
  float hours = float(std::min(seconds, 3600.0) / 3600.0), effort = std::clamp(intensity, 0.f, 3.f);
  if (r.energy > .15f && r.hunger < .95f && r.thirst < .95f && r.health > 25) {
    r.fitness = std::clamp(r.fitness + hours * effort * 1.5f * (1 - r.fitness / 100.f), 0.f, 100.f);
    r.strength =
        std::min(100.f, r.strength + hours * effort * .8f * std::max(0.f, 1 - r.strength / 100.f));
  }
  r.energy = std::max(0.f, r.energy - hours * effort * .035f);
  r.thirst = std::min(1.4f, r.thirst + hours * effort * .025f);
  r.hunger = std::min(1.4f, r.hunger + hours * effort * .015f);
}
Vec3 World::coarsePosition(Id id) const {
  const auto &r = residents.at(id);
  if (r.route.empty())
    return r.pos;
  if (time <= r.route.front().time)
    return r.route.front().p;
  for (std::size_t i = 1; i < r.route.size(); ++i)
    if (time < r.route[i].time) {
      auto &a = r.route[i - 1];
      auto &b = r.route[i];
      return a.p + (b.p - a.p) * float((time - a.time) / (b.time - a.time));
    }
  return r.route.back().p;
}
Vec3 World::position(Id id) const {
  const auto &r = residents.at(id);
  if (r.lift != none)
    return liftPassengerPosition(id);
  if (r.ambulance != none)
    return ambulancePosition(r.ambulance);
  if (r.carriedBy != none)
    return position(r.carriedBy) + Vec3{0, .7f, .8f};
  auto it = crowd.find(id);
  return it == crowd.end() ? coarsePosition(id) : it->second.pos;
}
const std::vector<Obstacle> &World::floorObstacles(int floor) const {
  auto it = obstacles.find(floor);
  if (it != obstacles.end())
    return it->second;
  auto &out = obstacles[floor];
  auto l = layout(floor);
  float y = floor * 3.f;
  auto add = [&](Vec3 p, Vec3 size) { out.push_back({p - size * .5f, p + size * .5f}); };
  auto homes = floorHomes.find(floor);
  if (homes != floorHomes.end()) {
    std::set<int> bunks;
    for (Id id : homes->second) {
      const auto &h = housing[id];
      if (!h.active)
        continue;
      auto p = homePosition(id);
      if (h.custom) {
        add(p + Vec3{0, .55f, 0}, {1.18f, 1.1f, 2.5f});
        continue;
      }
      int base = h.bed % l.tierSize();
      if (!bunks.insert(base).second)
        continue;
      add(p + Vec3{0, 1.4f, 0}, {1.18f, 2.8f, 2.5f});
      if ((base / l.banks() + floor) % 3 == 0)
        add(p + Vec3{1.2f, .6f, .4f}, {.6f, 1.2f, .7f});
    }
  }
  if (floor > 0)
    for (int side : {-1, 1}) {
      bool door = side == 1 && std::any_of(lifts.begin(), lifts.end(), [&](const LiftCar &c) {
                    const auto &stops = facilities[c.facility].stops;
                    return std::find(stops.begin(), stops.end(), floor) != stops.end();
                  });
      if (door) {
        for (int half : {-1, 1})
          add({l.shaftWidth() + .05f, y + .52f, half * (l.shaftDepth() / 2 + .8f)},
              {.12f, 1.04f, l.shaftDepth() - 1.6f});
      } else
        add({side * (l.shaftWidth() + .05f), y + .52f, 0}, {.12f, 1.04f, l.shaftDepth() * 2});
    }
  auto fit = floorFacilities.find(floor);
  if (fit != floorFacilities.end())
    for (Id id : fit->second) {
      auto &f = facilities[id];
      if (!f.demolished)
        add(f.pos + Vec3{0, .6f, 0}, {1.9f, 1.2f, 1.3f});
    }
  for (int side : {-1, 1}) {
    add({side * 10.f, y + .4f, 7.5f}, {1.7f, .8f, 1.2f});
    add({side * 10.f, y + .5f, 9}, {.7f, 1, .7f});
  }
  if (floor == 0 && config.layoutVersion > 1) {
    float z = l.halfDepth() - 46;
    for (int side : {-1, 1})
      for (int row = 0; row < 3; ++row)
        add({side * 6.f, .4f, z - row * 3}, {3, .8f, .7f});
  }
  for (const auto &o : out)
    for (int z = int(std::floor(o.min.z - .4f)); z <= int(std::floor(o.max.z + .4f)); ++z)
      for (int x = int(std::floor(o.min.x - .4f)); x <= int(std::floor(o.max.x + .4f)); ++x)
        obstacleCells[obstacleKey(floor, x, z)].push_back(o);
  return out;
}
bool World::clearPosition(Vec3 p, float radius) const {
  int f = std::clamp(int(std::floor((p.y + .05f) / 3)), 0, config.floors - 1);
  auto l = layout(f);
  if (std::abs(p.x) > l.halfWidth() - radius || std::abs(p.z) > l.halfDepth() - radius)
    return false;
  if (!l.supported(p))
    return false;
  (void)floorObstacles(f);
  if (radius <= .4f) {
    auto it = obstacleCells.find(obstacleKey(f, int(std::floor(p.x)), int(std::floor(p.z))));
    if (it != obstacleCells.end())
      for (const auto &o : it->second)
        if (overlaps(p, o, radius))
          return false;
  } else
    for (const auto &o : floorObstacles(f))
      if (overlaps(p, o, radius))
        return false;
  return true;
}
Vec3 World::accessible(Vec3 p) const {
  if (clearPosition(p))
    return p;
  for (float distance = .4f; distance < 5; distance += .4f)
    for (int a = 0; a < 16; ++a) {
      Vec3 q = p + Vec3{std::sin(a * .392699f) * distance, 0, std::cos(a * .392699f) * distance};
      if (clearPosition(q))
        return q;
    }
  throw std::runtime_error("Destination has no accessible standing space");
}
std::vector<Vec3> World::floorPath(Vec3 from, Vec3 to) const {
  int floor = std::clamp(int(std::round(to.y / 3)), 0, config.floors - 1);
  from = accessible(from);
  to = accessible(to);
  auto clearLine = [&](Vec3 a, Vec3 b) {
    int steps = std::max(1, int(std::ceil(length(b - a) / .35f)));
    for (int i = 0; i <= steps; ++i)
      if (!clearPosition(a + (b - a) * (float(i) / steps)))
        return false;
    return true;
  };
  if (clearLine(from, to))
    return {from, to};
  auto l = layout(floor);
  // Coarse search is cheap in open corridors; quarter-metre fallback resolves
  // the 0.9 m gaps between bunks without shrinking residents' collision bodies.
  auto search = [&](float step, float margin) -> std::vector<Vec3> {
    int x0 = int(std::ceil(std::max(-l.halfWidth() + .4f, std::min(from.x, to.x) - margin) / step));
    int x1 = int(std::floor(std::min(l.halfWidth() - .4f, std::max(from.x, to.x) + margin) / step));
    int z0 = int(std::ceil(std::max(-l.halfDepth() + .4f, std::min(from.z, to.z) - margin) / step));
    int z1 = int(std::floor(std::min(l.halfDepth() - .4f, std::max(from.z, to.z) + margin) / step));
    int width = x1 - x0 + 1, depth = z1 - z0 + 1;
    if (width <= 0 || depth <= 0)
      return {};
    int n = width * depth;
    std::vector<signed char> free(n, -1);
    auto point = [&](int i) {
      return Vec3{(i % width + x0) * step, to.y, (i / width + z0) * step};
    };
    auto walkable = [&](int i) {
      if (free[i] < 0)
        free[i] = clearPosition(point(i));
      return free[i] == 1;
    };
    auto closest = [&](Vec3 p) {
      int best = -1;
      float score = 1e20f;
      int px = int(std::round(p.x / step)), pz = int(std::round(p.z / step));
      int radius = int(std::ceil(2 / step));
      for (int z = std::max(z0, pz - radius); z <= std::min(z1, pz + radius); ++z)
        for (int x = std::max(x0, px - radius); x <= std::min(x1, px + radius); ++x) {
          int i = (z - z0) * width + x - x0;
          float d = length(point(i) - p);
          if (d < score && walkable(i) && clearLine(p, point(i))) {
            best = i;
            score = d;
          }
        }
      return best;
    };
    int start = closest(from), end = closest(to);
    if (start < 0 || end < 0)
      return {};
    std::vector<float> cost(n, 1e20f);
    std::vector<int> parent(n, -1);
    std::vector<bool> closed(n);
    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    cost[start] = 0;
    open.push({length(point(start) - point(end)), start});
    while (!open.empty()) {
      int i = open.top().second;
      open.pop();
      if (closed[i])
        continue;
      closed[i] = true;
      if (i == end)
        break;
      for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
          if (!dx && !dz)
            continue;
          int x = i % width + dx, z = i / width + dz;
          if (x < 0 || z < 0 || x >= width || z >= depth)
            continue;
          int j = z * width + x;
          if (closed[j] || !walkable(j))
            continue;
          if (dx && dz && (!walkable(i + dx) || !walkable(i + dz * width)))
            continue;
          float c = cost[i] + step * (dx && dz ? 1.414214f : 1.f);
          if (c >= cost[j] || !clearLine(point(i), point(j)))
            continue;
          cost[j] = c;
          parent[j] = i;
          open.push({c + length(point(j) - point(end)), j});
        }
    }
    if (start != end && parent[end] < 0)
      return {};
    std::vector<Vec3> reverse;
    for (int i = end; i != start; i = parent[i])
      reverse.push_back(point(i));
    reverse.push_back(point(start));
    std::vector<Vec3> result{from};
    for (auto it = reverse.rbegin(); it != reverse.rend(); ++it)
      result.push_back(*it);
    result.push_back(to);
    for (std::size_t i = 1; i + 1 < result.size();) {
      if (length(cross(result[i] - result[i - 1], result[i + 1] - result[i])) < .001f &&
          clearLine(result[i - 1], result[i + 1]))
        result.erase(result.begin() + i);
      else
        ++i;
    }
    return result;
  };
  for (auto [step, margin] : {std::pair{1.f, 24.f}, std::pair{.25f, 24.f}, std::pair{.25f, 64.f}}) {
    auto path = search(step, margin);
    if (!path.empty())
      return path;
  }
  throw std::runtime_error("No navigable route to this destination");
}

void World::routeTo(Id id, Vec3 dest) {
  auto &r = residents.at(id);
  Vec3 p = position(id);
  const Vec3 start = p;
  std::vector<Waypoint> planned{{p, time}};
  double at = time;
  auto point = [&](Vec3 next) {
    float distance = length(next - p);
    if (distance < .001f)
      return;
    double speed =
        (std::abs(next.y - p.y) > .1f ? 3.0 : walkingSpeed(id)) / (1440 / config.dayMinutes);
    at += distance / speed;
    planned.push_back({next, at});
    p = next;
  };
  auto path = [&](Vec3 target) {
    auto path = floorPath(p, target);
    for (auto q : path)
      point(q);
  };
  if (std::abs(p.y - dest.y) > .1f) {
    int ladder =
        config.layoutVersion == 1 ? 0 : (p.z + dest.z >= 0 ? 0 : 2) + (dest.y < p.y ? 1 : 0);
    auto l = layout();
    path(l.landing(ladder, p.y));
    point(l.ladder(ladder, p.y));
    point(l.ladder(ladder, dest.y));
    point(l.landing(ladder, dest.y));
  }
  path(dest);
  r.pos = start;
  r.route = std::move(planned);
  ++r.routeVersion;
  if (auto it = crowd.find(id); it != crowd.end()) {
    it->second.version = r.routeVersion;
    it->second.next = 1;
  }
}
void World::focus(int floor) {
  focusFloor = floor;
  nearbyResidents.clear();
  for (auto &r : residents) {
    Vec3 p = position(r.id);
    if (r.alive && r.present && std::abs(p.y - floor * 3.f) < 6 &&
        r.activity != Activity::Sleeping && r.vehicle == none && r.ambulance == none &&
        r.carriedBy == none && r.lift == none) {
      nearbyResidents.push_back(r.id);
      if (!crowd.contains(r.id)) {
        std::size_t next = 1;
        while (next < r.route.size() && r.route[next].time < time)
          ++next;
        crowd.emplace(r.id, CrowdBody{p, r.routeVersion, next});
      }
    } else if (auto it = crowd.find(r.id); it != crowd.end()) {
      // Preserve delayed movement when changing simulation detail level.
      Vec3 actual = it->second.pos;
      auto next = it->second.next;
      crowd.erase(it);
      if (!r.route.empty() && next < r.route.size()) {
        std::vector<Waypoint> route{{actual, time}};
        double at = time;
        Vec3 p = actual;
        for (std::size_t i = next; i < r.route.size(); ++i) {
          at += length(r.route[i].p - p) / walkingSpeed(r.id) * (1440 / config.dayMinutes);
          p = r.route[i].p;
          route.push_back({p, at});
        }
        r.route = std::move(route);
      } else
        r.pos = actual;
    }
  }
}
Vec3 World::moveCapsule(Id id, Vec3 p, Vec3 delta, bool people) const {
  const auto &r = residents.at(id);
  int steps = std::max(1, int(std::ceil(length(delta) / .12f)));
  Vec3 step = delta * (1.f / steps);
  auto blocked = [&](Vec3 q) {
    int floor = std::clamp(int(std::floor((q.y + .02f) / 3)), 0, config.floors - 1);
    auto clearance = [](Vec3 a, const Obstacle &o) {
      float dx = std::max({o.min.x - a.x, 0.f, a.x - o.max.x});
      float dz = std::max({o.min.z - a.z, 0.f, a.z - o.max.z});
      if (dx > 0 || dz > 0)
        return std::hypot(dx, dz);
      return -std::min({a.x - o.min.x, o.max.x - a.x, a.z - o.min.z, o.max.z - a.z});
    };
    for (const auto &o : floorObstacles(floor))
      if (overlaps(q, o) && (!overlaps(p, o) || clearance(q, o) <= clearance(p, o) + .000001f))
        return true;
    for (const auto &v : vehicles)
      if (v.driver != id && std::abs(q.y - v.pos.y) < 1.7f) {
        Vec3 d = q - v.pos;
        float x = d.x * std::cos(v.yaw) - d.z * std::sin(v.yaw),
              z = d.x * std::sin(v.yaw) + d.z * std::cos(v.yaw);
        bool small = v.kind <= VehicleKind::Scooter;
        if (std::abs(x) < (small ? .45f : 1.2f) && std::abs(z) < (small ? 1.05f : 2.1f))
          return true;
      }
    if (people)
      for (int y = int(std::floor(q.y / 3)) - 1; y <= int(std::floor(q.y / 3)) + 1; ++y)
        for (int z = int(std::floor(q.z)) - 1; z <= int(std::floor(q.z)) + 1; ++z)
          for (int x = int(std::floor(q.x)) - 1; x <= int(std::floor(q.x)) + 1; ++x) {
            auto it = crowdGrid.find({x, y, z});
            if (it == crowdGrid.end())
              continue;
            for (Id other : it->second) {
              if (other == id)
                continue;
              const auto &o = residents[other];
              if (!o.alive || !o.present || o.activity == Activity::Sleeping || o.vehicle != none ||
                  o.ambulance != none || o.carriedBy != none || o.lift != none)
                continue;
              Vec3 d = q - position(other);
              if (std::abs(d.y) < 1.7f && d.x * d.x + d.z * d.z < .58f * .58f) {
                Vec3 previous = p - position(other);
                // A loaded world or wake-up can begin in contact. Permit only motion
                // increasing separation; this never lets a clear body enter another.
                float before = previous.x * previous.x + previous.z * previous.z;
                if (std::abs(previous.y) >= 1.7f || before >= .58f * .58f ||
                    d.x * d.x + d.z * d.z <= before + .000001f)
                  return true;
              }
            }
          }
    return false;
  };
  (void)r;
  for (int i = 0; i < steps; ++i) {
    Vec3 q = p + Vec3{step.x, 0, 0};
    if (!blocked(q))
      p = q;
    q = p + Vec3{0, 0, step.z};
    if (!blocked(q))
      p = q;
    q = p + Vec3{0, step.y, 0};
    if (!blocked(q))
      p = q;
  }
  return p;
}
void World::moveCrowd(double seconds) {
  if (focusFloor < 0 || seconds <= 0)
    return;
  double real = seconds / (1440 / config.dayMinutes);
  int steps = std::max(1, int(std::ceil(real * 30)));
  float dt = float(real / steps);
  for (int step = 0; step < steps; ++step) {
    crowdGrid.clear();
    for (Id id : nearbyResidents) {
      Vec3 p = position(id);
      crowdGrid[{int(std::floor(p.x)), int(std::floor(p.y / 3)), int(std::floor(p.z))}].push_back(
          id);
    }
    for (Id id : nearbyResidents) {
      auto &r = residents[id];
      auto &b = crowd[id];
      if (r.ladder >= 0 || r.activity == Activity::Sleeping)
        continue;
      if (r.route.empty()) {
        b.pos = r.pos;
        continue;
      }
      if (b.version != r.routeVersion) {
        b.version = r.routeVersion;
        b.next = 1;
      }
      while (b.next < r.route.size() && length(r.route[b.next].p - b.pos) < .025f &&
             (time >= r.route[b.next].time ||
              length(r.route[b.next].p - r.route[b.next - 1].p) > .01f))
        ++b.next;
      if (b.next >= r.route.size()) {
        r.pos = b.pos;
        continue;
      }
      Vec3 d = r.route[b.next].p - b.pos;
      bool vertical = std::abs(d.y) > .1f;
      Vec3 delta = normalized(d) * std::min(length(d), dt * (vertical ? 3.f : walkingSpeed(id)));
      Vec3 next = moveCapsule(id, b.pos, delta);
      // Try clear, right-biased arcs when another body occupies the direct line.
      // This also handles the last metre into a queue, where the old steering stopped.
      if (!vertical && length(next - b.pos) < length(delta) * .7f && length(d) > .08f) {
        Vec3 forward = normalized(Vec3{d.x, 0, d.z}), right{-forward.z, 0, forward.x};
        float best = length(next - b.pos);
        for (float angle : {.55f, 1.05f, 1.57f, -.55f, -1.05f, -1.57f}) {
          Vec3 step = (forward * std::cos(angle) + right * std::sin(angle)) * length(delta);
          auto candidate = moveCapsule(id, b.pos, step);
          float progress = length(candidate - b.pos) * (angle > 0 ? 1.f : .94f);
          if (progress > best && clearPosition(candidate)) {
            next = candidate;
            best = progress;
          }
        }
      }
      if (length(next - b.pos) > .01f)
        exercise(id, dt * (1440 / config.dayMinutes), vertical ? 1.5f : .2f);
      b.pos = next;
      r.pos = next;
    }
  }
}
float World::moveBody(Id id, Body &body, Vec3 desired, float dt) const {
  dt = std::clamp(dt, 0.f, .1f);
  int steps = std::max(1, int(std::ceil(dt * 120)));
  float impact = 0;
  for (int i = 0; i < steps; ++i) {
    float s = dt / steps, oldY = body.position.y;
    body.velocity.x = desired.x;
    body.velocity.z = desired.z;
    body.velocity.y -= 9.81f * s;
    Vec3 horizontal = moveCapsule(id, body.position, {desired.x * s, 0, desired.z * s});
    body.position = horizontal + Vec3{0, body.velocity.y * s, 0};
    int floor = int(std::floor((oldY + .0001f) / 3));
    float plane = floor * 3.f;
    body.grounded = false;
    if (floor >= 0 && floor < config.floors && body.velocity.y <= 0 && body.position.y <= plane &&
        layout(floor).supported({body.position.x, plane, body.position.z})) {
      impact = std::max(impact, -body.velocity.y);
      body.position.y = plane;
      body.velocity.y = 0;
      body.grounded = true;
    }
  }
  return impact;
}
int World::nearbyLadder(Id id) const {
  Vec3 p = position(id);
  auto l = layout();
  if (std::abs(p.y - std::round(p.y / 3) * 3) > .15f)
    return -1;
  for (int i = 0; i < l.ladderCount(); ++i)
    if (length(p - l.ladder(i, p.y)) < 2.2f)
      return i;
  return -1;
}
void World::grabLadder(Id id) {
  auto &r = residents.at(id);
  int l = nearbyLadder(id);
  if (r.lift != none || r.ambulance != none || r.carriedBy != none || r.medicalDuty != none ||
      l < 0 || !r.alive || !r.present || r.vehicle != none || r.surrendered ||
      r.activity == Activity::Jailed || r.activity == Activity::Restrained ||
      r.activity == Activity::Escorted)
    throw std::runtime_error("Approach a ladder landing first");
  Vec3 p = position(id);
  clockOut(id);
  leaveQueue(id);
  r.route.clear();
  ++r.routeVersion;
  r.pos = p;
  r.task = TaskKind::None;
  r.activity = Activity::Idle;
  r.ladder = l;
  r.ladderExit = false;
  r.ladderWait = false;
  r.ladderTarget = -1;
  r.ladderDirection = 0;
}
void World::releaseLadder(Id id) {
  auto &r = residents.at(id);
  if (r.ladder < 0)
    return;
  if (std::abs(r.pos.y - std::round(r.pos.y / 3) * 3) > .15f)
    throw std::runtime_error("Reach a floor landing before dismounting");
  r.ladderExit = true;
}
void World::climb(Id id, float direction, float seconds) {
  auto &r = residents.at(id);
  if (r.ladder < 0)
    return;
  seconds = std::clamp(seconds, 0.f, .1f);
  Vec3 p = position(id);
  auto l = layout();
  Vec3 target =
      r.ladderExit ? l.landing(r.ladder, std::round(p.y / 3) * 3) : l.ladder(r.ladder, p.y);
  Vec3 d = target - p;
  if (length(d) > .015f)
    p = moveCapsule(id, p, normalized(d) * std::min(length(d), seconds * 2.2f));
  else if (r.ladderExit) {
    r.ladder = -1;
    r.ladderExit = false;
  } else {
    int sign = direction > .1f ? 1 : direction < -.1f ? -1 : 0;
    if (!sign) {
      r.ladderWait = false;
      r.ladderDirection = 0;
    }
    if (sign && !r.ladderWait) {
      if (sign != r.ladderDirection) {
        r.ladderTarget = std::clamp(sign > 0 ? int(std::floor((p.y + .001f) / 3)) + 1
                                             : int(std::ceil((p.y - .001f) / 3)) - 1,
                                    0, config.floors - 1);
        r.ladderDirection = sign;
      }
      float targetY = r.ladderTarget * 3.f;
      float y = sign > 0 ? std::min(p.y + seconds * 3.f, targetY)
                         : std::max(p.y - seconds * 3.f, targetY);
      p = moveCapsule(id, p, {0, y - p.y, 0});
      if (std::abs(p.y - targetY) < .001f)
        r.ladderWait = true;
    }
  }
  if (length(p - position(id)) > .01f)
    exercise(id, seconds * (1440 / config.dayMinutes), 1.5f);
  r.pos = p;
  if (crowd.contains(id))
    crowd[id].pos = p;
}
bool World::climbing(Id id) const {
  auto &r = residents.at(id);
  if (r.ladder >= 0)
    return true;
  Vec3 p = position(id);
  if (std::abs(p.y - std::round(p.y / 3) * 3) < .05f)
    return false;
  for (int i = 0; i < layout().ladderCount(); ++i)
    if (length(p - layout().ladder(i, p.y)) < .4f)
      return true;
  return false;
}
} // namespace mega
