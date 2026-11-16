"""The end of an SSX course (custom layer, tools/ttre/custom.py run_finishes; user, 2026-11-16; docs/PORTALS.md "The way
back from a course"): where it ends, the field around that end (the finish: Action there leads back to sud0), the
invisible walls that keep the skier from riding off that field, and a boost out of a dip before the end.

    end      the lowest point of the course's AI paths (<course>.aip: they follow the racing line, so that is the
             finish's run-out), except on a course in DIPS, whose racing line dips below its end (Alaska: 80 units under
             it, then a climb of ~230 up a ramp as steep as 60 degrees, then down to the end): there the end of the
             chain of paths that leaves the lowest point (each path's end meets the next one's start).
    field    (the finish: it and FINISH_MARGIN round it) the ground round the end seen from above (a raster of the collision's top surface, CELL units, within
             REACH of the end): the cells the skier can ride to from the end without climbing more than RISE over it, a
             step at most STEP between neighbours, on faces no steeper than 60 degrees. Untracked's end, the user's case:
             a plateau ~350 across with raised rims and nothing beyond them; a radius of 15 round the end covered a
             corner of it.
    walls    on the field's edge, vertical, from 4 under its ground to WALL_HEIGHT over it, facing in (one-sided: from
             outside the skier passes through): where the ground beyond falls more than DROP or ends within OUTLOOK
             (a rim, a drop into the void); not where the racing line comes in (an AI path point outside the field
             within ENTRY), nor round an obstacle on the field (the field again within OUTLOOK), nor where the ground
             keeps rising (a slope, a building). The boundary's cell edges are chained and simplified (within SIMPLIFY).
    boost    on a course in DIPS: the dip's path from its lowest point up to its highest point after it, BOOST_RADIUS
             round it, pushes the skier on the snow along it up to BOOST_SPEED (core tcx_course_boost).

Reads the converted course's collision (.ttlv COLL) and the disc's AI paths; uses numpy.
"""
import collections
import math
import struct

import numpy as np

CELL = 2.0          # the raster's cell, units
REACH = 260.0       # the field: at most this far from the end (Untracked's plateau reaches ~230)
RISE = 8.0          # ... and at most this high over it
STEP = 2.5          # ... a step of at most this between neighbouring cells (~51 degrees)
WALKABLE = 0.5      # ... on faces whose normal z is at least this (60 degrees)
OUTLOOK = 60.0      # a wall where the ground beyond the edge ends or falls within this (rims: Untracked's are 25-45 wide)
DROP = 4.0          # ... falls: more than this under the edge's ground
ENTRY = 20.0        # no wall within this of a path point outside the field (the way in)
SIMPLIFY = 1.5      # the walls' corners stay within this of the field's cell edges
WALL_UNDER, WALL_HEIGHT = 4.0, 40.0
FINISH_UNDER, FINISH_OVER = 3.0, 15.0  # the finish: Ed's feet this far under / over the field's lowest / highest ground
FINISH_MARGIN = 4.0  # ... and this far out of it
DIPS = {'alaska'}
BOOST_RADIUS, BOOST_SPEED, BOOST_ACCEL = 25.0, 55.0, 70.0  # units, u/s, u/s^2 (his gravity up a 60 degree face: 34)

Course = collections.namedtuple('Course', 'end field walls boost')
Field = collections.namedtuple('Field', 'x0 y0 cell z_lo z_hi mask')  # mask[j, i]: cell centred at (x0 + i cell, y0 + j cell)


def ai_paths(aip, scale):
    """The .aip's paths as lists of points (TT units), as ssx.start_path reads them."""
    if not aip or len(aip) < 24:
        return []
    o = 16
    n_paths, n_starts = struct.unpack_from('<2I', aip, o)
    o += 8 + 4 * n_starts
    out = []
    for _ in range(n_paths):
        n_pts, n_ev = struct.unpack_from('<2I', aip, o + 28)
        p = struct.unpack_from('<3f', aip, o + 36)
        pts = [p]
        for k in range(n_pts):
            d = struct.unpack_from('<4f', aip, o + 72 + 16 * k)  # a leg: its unit direction and length
            p = tuple(p[c] + d[c] * d[3] for c in range(3))
            pts.append(p)
        out.append([tuple(c * scale for c in q) for q in pts])
        o += 72 + 16 * n_pts + 16 * n_ev
    return out


def end_and_boost(course, paths):
    """(end, boost points or None)."""
    low, i, k = min(((q, i, k) for i, p in enumerate(paths) for k, q in enumerate(p)), key=lambda a: a[0][2])
    if course not in DIPS:
        return low, None
    p = paths[i]
    top = max(range(k, len(p)), key=lambda m: p[m][2])
    boost = p[k:top + 1]
    seen = {i}
    while True:  # the chain of paths on from the dip's
        e = paths[i][-1]
        nxt = [j for j, q in enumerate(paths) if j not in seen and math.dist(q[0], e) < 8]
        if not nxt:
            return e, boost
        i = nxt[0]
        seen.add(i)


def raster(tris, x0, y0, nx, ny, ztop):
    """The top surface at each cell centre no higher than ztop: heights and |normal z| (nan where nothing)."""
    H = np.full((ny, nx), np.nan)
    NZ = np.full((ny, nx), np.nan)
    lo, hi = tris.min(axis=1), tris.max(axis=1)
    n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    length = np.linalg.norm(n, axis=1)
    nz = np.abs(n[:, 2]) / np.where(length > 0, length, 1)
    x1, y1 = x0 + (nx - 1) * CELL, y0 + (ny - 1) * CELL
    pick = (hi[:, 0] >= x0) & (lo[:, 0] <= x1) & (hi[:, 1] >= y0) & (lo[:, 1] <= y1) & (lo[:, 2] <= ztop) & (nz > 0.25)
    for t in np.nonzero(pick)[0]:
        a, b, c = tris[t]
        i0, i1 = max(0, math.ceil((lo[t, 0] - x0) / CELL)), min(nx - 1, math.floor((hi[t, 0] - x0) / CELL))
        j0, j1 = max(0, math.ceil((lo[t, 1] - y0) / CELL)), min(ny - 1, math.floor((hi[t, 1] - y0) / CELL))
        if i0 > i1 or j0 > j1:
            continue
        det = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(det) < 1e-12:
            continue
        X, Y = np.meshgrid(x0 + CELL * np.arange(i0, i1 + 1), y0 + CELL * np.arange(j0, j1 + 1))
        w1 = ((b[1] - c[1]) * (X - c[0]) + (c[0] - b[0]) * (Y - c[1])) / det
        w2 = ((c[1] - a[1]) * (X - c[0]) + (a[0] - c[0]) * (Y - c[1])) / det
        w3 = 1 - w1 - w2
        Z = w1 * a[2] + w2 * b[2] + w3 * c[2]
        inside = (w1 >= 0) & (w2 >= 0) & (w3 >= 0) & (Z <= ztop)
        h, g = H[j0:j1 + 1, i0:i1 + 1], NZ[j0:j1 + 1, i0:i1 + 1]
        up = inside & (np.isnan(h) | (Z > h))
        h[up] = Z[up]
        g[up] = nz[t]
    return H, NZ


def flood(H, NZ, start, top):
    ny, nx = H.shape
    R = np.zeros(H.shape, bool)
    R[start[1], start[0]] = True
    todo = collections.deque([start])
    while todo:
        i, j = todo.popleft()
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            a, b = i + di, j + dj
            if 0 < a < nx - 1 and 0 < b < ny - 1 and not R[b, a] and not np.isnan(H[b, a]) and NZ[b, a] >= WALKABLE \
                    and H[b, a] <= top and abs(H[b, a] - H[j, i]) <= STEP:
                R[b, a] = True
                todo.append((a, b))
    return R


def wall_edges(H, R, x0, y0, outside):
    """The field's cell edges that take a wall: (i, j, di, dj), cell (i, j) in the field, its neighbour (i + di, j + dj)
    not."""
    ny, nx = H.shape
    look = int(round(OUTLOOK / CELL))
    edges = []
    for j, i in zip(*np.nonzero(R)):
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if R[j + dj, i + di]:
                continue
            mx, my = x0 + (i + 0.5 * di) * CELL, y0 + (j + 0.5 * dj) * CELL
            if len(outside) and np.min(np.hypot(outside[:, 0] - mx, outside[:, 1] - my)) < ENTRY:
                continue
            wall = False
            for s in range(1, look + 1):
                a, b = i + di * s, j + dj * s
                if not (0 <= a < nx and 0 <= b < ny) or np.isnan(H[b, a]) or H[b, a] < H[j, i] - DROP:
                    wall = True
                    break
                if R[b, a]:
                    break
            if wall:
                edges.append((i, j, di, dj))
    return edges


def chains(edges):
    """The edges' boundary as polylines of grid corners (doubled cell coordinates: corner (2i + 1, 2j + 1) is the
    upper right one of cell (i, j))."""
    segs = []
    for i, j, di, dj in edges:
        if di:
            x = 2 * i + di
            segs.append(((x, 2 * j - 1), (x, 2 * j + 1)))
        else:
            y = 2 * j + dj
            segs.append(((2 * i - 1, y), (2 * i + 1, y)))
    at = collections.defaultdict(list)
    for k, (a, b) in enumerate(segs):
        at[a].append(k)
        at[b].append(k)
    used, out = set(), []

    def walk(k, frm):
        line = [frm]
        while True:
            used.add(k)
            a, b = segs[k]
            nxt = b if a == frm else a
            line.append(nxt)
            more = [m for m in at[nxt] if m not in used]
            if len(at[nxt]) != 2 or not more:
                return line
            k, frm = more[0], nxt

    for k in range(len(segs)):  # open chains from their ends first, then the loops
        for c in segs[k]:
            if k not in used and len(at[c]) != 2:
                out.append(walk(k, c))
    for k in range(len(segs)):
        if k not in used:
            out.append(walk(k, segs[k][0]))
    return out


def simplify(line, tol):
    """Douglas-Peucker."""
    if len(line) < 3:
        return line
    a, b = np.array(line[0], float), np.array(line[-1], float)
    d = b - a
    n = np.hypot(*d)
    pts = np.array(line[1:-1], float)
    dist = np.abs(d[0] * (pts[:, 1] - a[1]) - d[1] * (pts[:, 0] - a[0])) / n if n > 0 else np.hypot(*(pts - a).T)
    k = int(np.argmax(dist))
    if dist[k] <= tol:
        return [line[0], line[-1]]
    return simplify(line[:k + 2], tol)[:-1] + simplify(line[k + 1:], tol)


def walls(H, R, x0, y0, edges):
    """The wall triangles (3 points each), their fronts towards the field."""
    ny, nx = H.shape
    out = []

    def world(c):
        return x0 + c[0] * CELL / 2, y0 + c[1] * CELL / 2

    def cell(x, y):
        return int(round((x - x0) / CELL)), int(round((y - y0) / CELL))

    for line in chains(edges):
        pts = [world(c) for c in simplify(line, SIMPLIFY / (CELL / 2))]
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            dx, dy = bx - ax, by - ay
            length = math.hypot(dx, dy)
            if length < 1e-6:
                continue
            nxv, nyv = -dy / length, dx / length  # left of a -> b
            ground, inward = [], 0
            for s in np.linspace(0, 1, max(2, int(length / CELL) + 1)):
                for side in (1, -1):
                    i, j = cell(ax + dx * s + side * nxv * 0.7 * CELL, ay + dy * s + side * nyv * 0.7 * CELL)
                    if 0 <= i < nx and 0 <= j < ny and R[j, i]:
                        inward += side
                        ground.append(H[j, i])
            if not ground:
                continue
            if inward < 0:  # the field lies to the right: turn the segment round, so its left is the field
                ax, ay, bx, by = bx, by, ax, ay
                nxv, nyv = -nxv, -nyv
            z0, z1 = min(ground) - WALL_UNDER, max(ground) + WALL_HEIGHT
            # a -> b with the field on its left: wound so the front (the counter-clockwise normal) faces left
            a0, a1, b0, b1 = (ax, ay, z0), (ax, ay, z1), (bx, by, z0), (bx, by, z1)
            out += [(b0, a0, a1), (b0, a1, b1)]
    return out


def course_end(course, coll, aip, scale):
    """Course(end, Field, walls [(a, b, c)], boost [points] or None), or None (no AI paths). coll: the .ttlv's COLL."""
    paths = ai_paths(aip, scale)
    if not paths:
        return None
    end, boost = end_and_boost(course, paths)
    n, = struct.unpack_from('<I', coll)
    tris = np.frombuffer(coll, dtype='<f4', count=9 * n, offset=4).reshape(n, 3, 3).astype(np.float64)
    x0, y0 = end[0] - REACH, end[1] - REACH
    nx = ny = int(2 * REACH / CELL) + 1
    H, NZ = raster(tris, x0, y0, nx, ny, end[2] + 40)
    ci, cj = int(round((end[0] - x0) / CELL)), int(round((end[1] - y0) / CELL))
    near = [(di * di + dj * dj, ci + di, cj + dj) for dj in range(-6, 7) for di in range(-6, 7)
            if not np.isnan(H[cj + dj, ci + di]) and NZ[cj + dj, ci + di] >= WALKABLE and abs(H[cj + dj, ci + di] - end[2]) < 6]
    if not near:
        return None
    _, si, sj = min(near)
    R = flood(H, NZ, (si, sj), end[2] + RISE)
    pts = np.array([q for p in paths for q in p])
    pts = pts[(np.abs(pts[:, 0] - end[0]) < REACH) & (np.abs(pts[:, 1] - end[1]) < REACH)]
    cells = np.rint((pts[:, :2] - (x0, y0)) / CELL).astype(int)
    ok = (cells >= 0).all(axis=1) & (cells < nx).all(axis=1)
    inside = np.zeros(len(pts), bool)
    inside[ok] = R[cells[ok, 1], cells[ok, 0]]
    edges = wall_edges(H, R, x0, y0, pts[~inside])
    tri = walls(H, R, x0, y0, edges)
    ground = H[R]
    F = R.copy()  # the finish: the field and FINISH_MARGIN round it (against a wall his feet are in the next cell)
    for _ in range(int(round(FINISH_MARGIN / CELL))):
        F[1:-1, 1:-1] |= F[:-2, 1:-1] | F[2:, 1:-1] | F[1:-1, :-2] | F[1:-1, 2:]
    js, is_ = np.nonzero(F)
    i0, i1, j0, j1 = is_.min(), is_.max(), js.min(), js.max()
    field = Field(x0 + i0 * CELL, y0 + j0 * CELL, CELL, float(ground.min()) - FINISH_UNDER, float(ground.max()) + FINISH_OVER,
                  F[j0:j1 + 1, i0:i1 + 1])
    return Course(end, field, tri, boost)


def chunk(c, level, place, camera_place):
    """The .ttai's CRSE chunk (core/src/custom/tcx_course.c has the layout)."""
    def name(s):
        b = s.encode('latin-1')
        return struct.pack('<H', len(b)) + b

    def pad(b):
        return b + b'\0' * (-len(b) % 4)

    f = c.field
    ny, nx = f.mask.shape
    bits = np.packbits(f.mask.reshape(-1).astype(np.uint8), bitorder='little').tobytes()
    out = pad(name(level) + name(place) + name(camera_place)) + struct.pack('<3f', *c.end)
    out += pad(struct.pack('<5f2H', f.x0, f.y0, f.cell, f.z_lo, f.z_hi, nx, ny) + bits)
    boost = c.boost or []
    out += struct.pack('<I3f', len(boost), BOOST_RADIUS, BOOST_SPEED, BOOST_ACCEL)
    out += b''.join(struct.pack('<3f', *p) for p in boost)
    out += struct.pack('<I', len(c.walls)) + b''.join(struct.pack('<9f', *a, *b, *d) for a, b, d in c.walls)
    return out
