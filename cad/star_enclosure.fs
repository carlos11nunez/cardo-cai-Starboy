FeatureScript 2144;
import(path : "onshape/std/geometry.fs", version : "2144.0");

// Starboy keychain enclosure for Waveshare ESP32-S3-Touch-AMOLED-1.43 (bare board) + 3.7 V LiPo.
// Paste the WHOLE file into a Feature Studio, replacing everything in it, then Commit.
// Units: mm. Z=0 is the back, display faces +Z (Top plane), USB-C points down (-Y).
//
// Layout (like the reference renders):
//   - a chubby star PLATE with a FLAT front (like the real one), thick in the middle and tapering thin
//     toward the tips on the BACK side, so it prints face-down with no supports on the star itself
//   - a round POD on the back that holds the electronics, closed by a round BACK LID (4x M2 screws)
//   - a small LOOP TAB on the back, behind the top point, that holds the metal keyring
// Parts: "Front shell" (plate + pod) and "Back lid". The display glass sits flush with the front face.

// ---- Board (Waveshare dimension drawing) ----
const GLASS_D = 46.0;       // glass / board diameter
const BOARD_STACK = 6.2;    // glass front to PCB back
const COMP_H = 4.0;         // board's 4 mm standoffs + components behind the PCB (USB-C, connectors)
const HOLES = [[-14.25, 11.6], [14.25, 11.6], [-16.25, -11.5], [16.25, -11.5]]; // board M2 holes, +Y = BAT side
const FIT = 0.3;            // radial clearance
const GLASS_PROUD = 0.0;    // 0 = glass flush with the front face

// ---- Battery bay (3.7 V 502535 LiPo, 5x25x35 mm, ~400 mAh, plus a 1 mm foam pad) ----
const BAT_W = 27.0;
const BAT_L = 37.0;
const BAT_T = 6.0;

// ---- Star plate ----
// Star outline measured from the front-on product photo (display = 50 mm as the ruler):
// [angle in degrees, distance from center in mm] for each tip and inner corner, counter-clockwise
// from the right point. Fat middle (the display nearly reaches the inner corners), round tips,
// and the two bottom points are the longest. Tip distances are before rounding (they end ~7 mm shorter).
const STAR = [[21, 57], [53, 35],     // right tip, upper-right corner
              [86, 57], [123, 32],    // top tip,   upper-left corner
              [160, 55], [192, 33],   // left tip,  lower-left corner
              [223, 65], [270, 35],   // bottom-left tip, bottom corner
              [316, 70], [348, 37]];  // bottom-right tip, lower-right corner
const TIP_R = 8.0;          // round tips like the photo
const VALLEY_R = 8.0;       // must stay bigger than PLATE_EDGE_R
const TOP_ANGLE = 86.0;     // the top tip's direction (keyring loop tab sits behind it)
const PLATE_T = 12.0;       // thickness in the middle (chunky)
const TIP_T = 7.0;          // thickness at the tips (must exceed 2x PLATE_EDGE_R: both faces are rounded)
const R_FLAT = 30.0;        // back stays flat out to this radius, then slopes toward the front at the tips
const R_TIP = 63.0;         // radius where the plate reaches TIP_T (the long bottom tips)
const PLATE_EDGE_R = 3.0;   // puffy round-over on the front and back edges

// ---- Back pod + lid ----
const POD_OD = 58.0;
const LID_T = 2.0;
const LID_SCREW_R = 25.5;   // 4 screws at 45/135/225/315 degrees


// ---- Holes ----
const USB_W = 12.5;         // fits a USB-C plug overmold
const USB_H = 7.0;
const LOOP_HOLE = 3.5;      // for the metal keyring
const LOOP_R = 38.0;        // loop tab distance from center, behind the top point (just outside the pod)
const LOOP_DROP = 1.0;      // how far the loop tab sticks out the back: tab length = this + 4 mm (was 2.5)
// shortcut: button angles estimated from the board photo, verify on the real board before printing.
const BUTTON_ANGLES = [27.0, -13.0, -58.0]; // RST, BOOT, PWR
const PIN_D = 2.0;

// ---- Stack (z, back to front) ----
const Z_BAY = LID_T;
const Z_FLOOR = Z_BAY + BAT_T;
const Z_COMP = Z_FLOOR + 1.6;
const Z_PCB = Z_COMP + COMP_H;
const Z_GLASS = Z_PCB + BOARD_STACK;
const Z_FRONT = Z_GLASS - GLASS_PROUD;   // plate front face (flat middle)
const Z_PLATE = Z_FRONT - PLATE_T;       // plate back face

function mm(x, y, z) returns Vector
{
    return vector(x, y, z) * millimeter;
}

function makeStarPrism(context is Context, id is Id, z0 is number, h is number) returns Query
{
    var pts = [];
    for (var i = 0; i < size(STAR); i += 1)
    {
        const r = STAR[i][1];
        const a = STAR[i][0] * degree;
        pts = append(pts, vector(r * cos(a), r * sin(a)) * millimeter);
    }
    pts = append(pts, pts[0]);
    const sk = newSketchOnPlane(context, id + "sk", { "sketchPlane" : plane(mm(0, 0, z0), vector(0, 0, 1)) });
    skPolyline(sk, "star", { "points" : pts });
    skSolve(sk);
    opExtrude(context, id + "ext", {
            "entities" : qSketchRegion(id + "sk"),
            "direction" : vector(0, 0, 1),
            "endBound" : BoundingType.BLIND,
            "endDepth" : h * millimeter });
    opDeleteBodies(context, id + "delSk", { "entities" : qCreatedBy(id + "sk", EntityType.BODY) });
    const body = qCreatedBy(id + "ext", EntityType.BODY);

    // round tips and valleys with different radii (classify vertical edges by distance from center)
    var tips = [];
    var valleys = [];
    for (var e in evaluateQuery(context, qParallelEdges(qOwnedByBody(body, EntityType.EDGE), vector(0, 0, 1))))
    {
        const o = evLine(context, { "edge" : e }).origin;
        if (norm(vector(o[0], o[1])) > 43 * millimeter)  // tips are all > 50 mm out, corners < 37
            tips = append(tips, e);
        else
            valleys = append(valleys, e);
    }
    opFillet(context, id + "tipF", { "entities" : qUnion(tips), "radius" : TIP_R * millimeter });
    opFillet(context, id + "valF", { "entities" : qUnion(valleys), "radius" : VALLEY_R * millimeter });
    return body;
}

function cutCyl(context is Context, id is Id, targets is Query, x is number, y is number, z0 is number, z1 is number, d is number)
{
    fCylinder(context, id + "t", { "bottomCenter" : mm(x, y, z0), "topCenter" : mm(x, y, z1), "radius" : d / 2 * millimeter });
    opBoolean(context, id + "b", { "tools" : qCreatedBy(id + "t", EntityType.BODY), "targets" : targets,
                "operationType" : BooleanOperationType.SUBTRACTION });
}

function cutBox(context is Context, id is Id, targets is Query, c1 is Vector, c2 is Vector)
{
    fCuboid(context, id + "t", { "corner1" : c1, "corner2" : c2 });
    opBoolean(context, id + "b", { "tools" : qCreatedBy(id + "t", EntityType.BODY), "targets" : targets,
                "operationType" : BooleanOperationType.SUBTRACTION });
}

annotation { "Feature Type Name" : "Starboy enclosure" }
export const starboyEnclosure = defineFeature(function(context is Context, id is Id, definition is map)
    precondition
    {
    }
    {
        // ---- tapered star plate: flat front; the back slopes up toward the tips (star prism intersected with a cone) ----
        const star = makeStarPrism(context, id + "star", Z_PLATE, PLATE_T);
        const coneBaseR = R_FLAT + PLATE_T * (R_TIP - R_FLAT) / (PLATE_T - TIP_T); // where the back slope would reach the front face
        fCone(context, id + "taper", { "bottomCenter" : mm(0, 0, Z_PLATE), "topCenter" : mm(0, 0, Z_FRONT),
                    "bottomRadius" : R_FLAT * millimeter, "topRadius" : coneBaseR * millimeter });
        opBoolean(context, id + "taperB", { "tools" : qUnion([star, qCreatedBy(id + "taper", EntityType.BODY)]),
                    "operationType" : BooleanOperationType.INTERSECTION });
        // find the resulting plate by a point inside its top point
        const plate = qContainsPoint(qCreatedBy(id, EntityType.BODY), mm(40 * cos(TOP_ANGLE * degree), 40 * sin(TOP_ANGLE * degree), Z_FRONT - 3));

        // round over the outer edges, front and back (everything except the flat back center's edge)
        try
        {
            const back = qCoincidesWithPlane(qOwnedByBody(plate, EntityType.FACE), plane(mm(0, 0, Z_PLATE), vector(0, 0, 1)));
            opFillet(context, id + "plateF", { "entities" : qSubtraction(qOwnedByBody(plate, EntityType.EDGE),
                                qAdjacent(back, AdjacencyType.EDGE, EntityType.EDGE)), "radius" : PLATE_EDGE_R * millimeter });
        }
        catch (error)
        {
            reportFeatureWarning(context, id, "Plate edge rounding failed - try a smaller PLATE_EDGE_R");
        }

        // ---- back pod + keyring loop tab on the back, merged with the plate into the front shell ----
        fCylinder(context, id + "pod", { "bottomCenter" : mm(0, 0, Z_BAY), "topCenter" : mm(0, 0, Z_PLATE + 0.5), "radius" : POD_OD / 2 * millimeter });
        // loop tab: sticks out of the plate's back behind the top point, rounded bottom, sideways hole
        const lx = LOOP_R * cos(TOP_ANGLE * degree);
        const ly = LOOP_R * sin(TOP_ANGLE * degree);
        const lz = Z_PLATE - LOOP_DROP;   // hole center, this far behind the plate (tab sticks out LOOP_DROP + 4 mm)
        fCuboid(context, id + "tab", { "corner1" : mm(lx - 3.5, ly - 4, lz), "corner2" : mm(lx + 3.5, ly + 4, Z_PLATE + 3.5) });  // reaches into the sloped back
        fCylinder(context, id + "tabEnd", { "bottomCenter" : mm(lx - 3.5, ly, lz), "topCenter" : mm(lx + 3.5, ly, lz), "radius" : 4 * millimeter });
        opBoolean(context, id + "merge", { "tools" : qUnion([plate, qCreatedBy(id + "pod", EntityType.BODY),
                            qCreatedBy(id + "tab", EntityType.BODY), qCreatedBy(id + "tabEnd", EntityType.BODY)]),
                    "operationType" : BooleanOperationType.UNION });
        // find the merged body by a point inside the top star point
        const shell = qContainsPoint(qCreatedBy(id, EntityType.BODY), mm(40 * cos(TOP_ANGLE * degree), 40 * sin(TOP_ANGLE * degree), Z_FRONT - 3));
        fCylinder(context, id + "loop", { "bottomCenter" : mm(lx - 5, ly, lz), "topCenter" : mm(lx + 5, ly, lz), "radius" : LOOP_HOLE / 2 * millimeter });
        opBoolean(context, id + "loopB", { "tools" : qCreatedBy(id + "loop", EntityType.BODY), "targets" : shell,
                    "operationType" : BooleanOperationType.SUBTRACTION });

        // ---- back lid: round cap on the pod ----
        fCylinder(context, id + "lid", { "bottomCenter" : mm(0, 0, 0), "topCenter" : mm(0, 0, LID_T), "radius" : POD_OD / 2 * millimeter });
        const lid = qCreatedBy(id + "lid", EntityType.BODY);
        try silent
        {
            const lidBack = qCoincidesWithPlane(qOwnedByBody(lid, EntityType.FACE), plane(mm(0, 0, 0), vector(0, 0, 1)));
            opFillet(context, id + "lidF", { "entities" : qAdjacent(lidBack, AdjacencyType.EDGE, EntityType.EDGE), "radius" : 1.5 * millimeter });
        }

        const pocketD = GLASS_D + 2 * FIT;

        // front pocket for glass + PCB (board drops in from the front)
        cutCyl(context, id + "pocket", shell, 0, 0, Z_PCB, Z_FRONT + 5, pocketD);

        // component cavity: open all the way across. The board has its own 4 mm brass M2 standoffs
        // ("M2.00-H4" on the Waveshare drawing) that rest right on the floor here.
        cutCyl(context, id + "cav", shell, 0, 0, Z_COMP, Z_PCB + 0.1, pocketD);

        // battery bay inside the pod, open to the lid
        cutBox(context, id + "bay", shell, mm(-BAT_W / 2, -BAT_L / 2, Z_BAY - 1), mm(BAT_W / 2, BAT_L / 2, Z_FLOOR));

        // M2 screw holes through the floor, up into the board's standoffs (M2x5 from the battery side)
        for (var i = 0; i < size(HOLES); i += 1)
        {
            cutCyl(context, id + ("bhole" ~ i), shell, HOLES[i][0], HOLES[i][1], Z_BAY, Z_PCB, 2.4);
        }

        // battery wire pass-through near the BAT connector
        cutBox(context, id + "wire", shell, mm(-4, 7.5, Z_FLOOR - 0.1), mm(4, 10.5, Z_COMP + 0.1));

        // USB-C charging slot out through the bottom
        const usbZ = Z_PCB - 2.0;
        cutBox(context, id + "usb", shell, mm(-USB_W / 2, -70, usbZ - USB_H / 2), mm(USB_W / 2, -15, usbZ + USB_H / 2));

        // button pinholes (radial, paperclip access)
        for (var i = 0; i < size(BUTTON_ANGLES); i += 1)
        {
            const a = BUTTON_ANGLES[i] * degree;
            const z = Z_PCB - 1.5;
            fCylinder(context, id + ("pin" ~ i), { "bottomCenter" : mm(20 * cos(a), 20 * sin(a), z),
                        "topCenter" : mm(70 * cos(a), 70 * sin(a), z), "radius" : PIN_D / 2 * millimeter });
            opBoolean(context, id + ("pinB" ~ i), { "tools" : qCreatedBy(id + ("pin" ~ i), EntityType.BODY), "targets" : shell,
                        "operationType" : BooleanOperationType.SUBTRACTION });
        }

        // lid screws: pilot in the pod wall, clearance + counterbore in the lid
        for (var i = 0; i < 4; i += 1)
        {
            const a = (45 + 90 * i) * degree;
            const x = LID_SCREW_R * cos(a);
            const y = LID_SCREW_R * sin(a);
            cutCyl(context, id + ("pilot" ~ i), shell, x, y, Z_BAY - 0.1, Z_BAY + 6, 1.8);
            cutCyl(context, id + ("clr" ~ i), lid, x, y, -1, LID_T + 0.1, 2.4);
            cutCyl(context, id + ("cb" ~ i), lid, x, y, -1, 1.2, 4.2);
        }

        setProperty(context, { "entities" : shell, "propertyType" : PropertyType.NAME, "value" : "Front shell" });
        setProperty(context, { "entities" : lid, "propertyType" : PropertyType.NAME, "value" : "Back lid" });
    });
