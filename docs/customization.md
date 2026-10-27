# Customization architecture

## Executable now

Appearance belongs to the persistent object, rather than to a global model. Shared geometry stays immutable. A resident's worn clothing references its own saved colour/style/accessory record; dirt darkens the fabric and laundry restores cleanliness. Nearby residents use rigged CC0 body meshes with their saved shirt/trouser colors and jacket/pocket details, caps and backpacks. Fabric regions blend over the base body; separate garment geometry is still needed. Distant residents retain cheaper procedural figures.

Each housing unit saves frame, linen, blanket and cabinet finishes, a privacy curtain and cabinet opening state. A tenant can change linens/curtains and operate the cabinet, while repainting landlord-owned fixtures requires ownership. The imported cabinet retains its original door and drawer pivots. Door movement is eased over time. The selected resident's detailed cabinet is positioned at their actual bed; distant furniture remains simplified.

Owned vehicles have independent paint, accessory finish and rack/box options. The sedan/van GLBs preserve named body and wheel parts; painting the body leaves wheel materials intact. Changes are rejected while a vehicle is occupied or moving. DORMNET → Customize exposes home, wardrobe and vehicle controls; vehicle selection cycles through owned vehicles. All settings survive save/load.

These are currently free cosmetic configuration options. They do not grant capacity, protection, speed or extra inventory. Purchasing real replacement parts, material deliveries, paid tailoring/repainting, installation time, compatibility-dependent handling and collision dimensions are not yet implemented. Those changes should be fulfilled by workshop jobs instead of becoming instant upgrades.

## Next character pipeline

Use one compatible humanoid skeleton per body family, with separate skinned head/hair/top/bottom/shoes/accessory meshes. Retarget locomotion and interaction clips once per skeleton. Define body-size compatibility and hide covered body regions so clothing does not intersect skin. Keep skin, hair, fabric, trims, dirt and damage as separate material inputs. Bake lower-detail meshes for crowds and share animation samples among distant residents; retain full identities and possessions in the simulation.

The importer now supports static glTF/GLB/OBJ plus glTF/GLB joint hierarchies, inverse-bind matrices and up to eight skin influences. Blended procedural activity poses have CPU and Vulkan compute skinning paths. Shared geometry remains immutable; per-resident palettes and appearance data deform into separate render-buffer ranges. Software Vulkan and limited storage fall back to the CPU path. See `rendering.md`. Imported animation clips, retargeting, body morphs, independently fitted garment meshes and cloth simulation remain to be built.

## Ownership and crime

Legitimate customization checks the resident's possessions and tenancy/ownership. Theft uses a separate physical action: the target must be within 1.5 metres, the item must actually be carried, and worn, reserved or delivery items cannot be snatched. A successful theft moves one item between inventories, retaining its original owner as provenance. Repeating the theft cannot duplicate the item. Noticed attempts create theft incidents; threatening robbery is marked separately and can lead to fighting or surrender. Some aggressive queued NPCs attempt theft rarely. The nearby-resident panel exposes attempted theft to the player.

A comprehensive stolen-goods investigation/recovery, fences, vehicle theft, lockpicking, burglary and threatening-dialogue system remain pending. Wardens use the existing physical incident response and escort paths.
