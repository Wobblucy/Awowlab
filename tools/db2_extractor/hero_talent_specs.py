"""
Which hero talent trees each spec can choose.

This table is written by hand, which is worth explaining because nothing else
in this directory is.

The client data names every hero tree and says which node entries belong to
one, so build_hero_talents.py can label an imported fight without any help.
What it cannot find is which *spec* may pick a given tree. The obvious routes
were checked against the 12.1 exports and none of them carry it:

  - traitsubtree.TraitTreeID is class-level. A Demon Hunter's Fel-Scarred,
    Aldrachi Reaver and Annihilator all sit on tree 854.
  - traitcond.SpecSetID gates 158 node groups, none of which contain a hero
    node. That mechanism is for ordinary talents.
  - traittreeloadout maps a tree to its specs, but again per class: tree 854
    gives {577, 581, 1480}, every DH spec, which cannot separate the three.

Deriving it from imported logs was tried and is worse: the list then only holds
trees somebody happened to have played, so Havoc showed Fel-Scarred alone
because no cached character had taken Aldrachi Reaver.

So this is maintained by hand and must be revisited when a patch adds a spec or
a tree. The tree names themselves are still generated - anything here that does
not match a name in traitsubtree is reported rather than written out, which
catches a rename without anyone having to notice it.
"""

# Spec id -> the hero trees that spec can take.
#
# Ids match parser::allSpecIds in SpecializationColors.h. The "initial" spec
# each class carries (1444-1456) has no hero talents and is left out.
SPEC_HERO_TREES = {
    # Death Knight
    250:  ["Deathbringer", "San'layn"],                 # Blood
    251:  ["Deathbringer", "Rider of the Apocalypse"],  # Frost
    252:  ["Rider of the Apocalypse", "San'layn"],      # Unholy

    # Demon Hunter
    577:  ["Aldrachi Reaver", "Fel-Scarred"],           # Havoc
    581:  ["Aldrachi Reaver", "Fel-Scarred"],           # Vengeance
    1480: ["Annihilator", "Void-Scarred"],              # Devourer

    # Druid
    102:  ["Elune's Chosen", "Keeper of the Grove"],    # Balance
    103:  ["Druid of the Claw", "Wildstalker"],         # Feral
    104:  ["Druid of the Claw", "Elune's Chosen"],      # Guardian
    105:  ["Keeper of the Grove", "Wildstalker"],       # Restoration

    # Evoker
    1467: ["Flameshaper", "Scalecommander"],            # Devastation
    1468: ["Chronowarden", "Flameshaper"],              # Preservation
    1473: ["Chronowarden", "Scalecommander"],           # Augmentation

    # Hunter
    253:  ["Dark Ranger", "Pack Leader"],               # Beast Mastery
    254:  ["Dark Ranger", "Sentinel"],                  # Marksmanship
    255:  ["Pack Leader", "Sentinel"],                  # Survival

    # Mage
    62:   ["Spellslinger", "Sunfury"],                  # Arcane
    63:   ["Frostfire", "Sunfury"],                     # Fire
    64:   ["Frostfire", "Spellslinger"],                # Frost

    # Monk
    268:  ["Master of Harmony", "Shado-Pan"],           # Brewmaster
    269:  ["Conduit of the Celestials", "Shado-Pan"],   # Windwalker
    270:  ["Conduit of the Celestials", "Master of Harmony"],  # Mistweaver

    # Paladin
    65:   ["Herald of the Sun", "Lightsmith"],          # Holy
    66:   ["Lightsmith", "Templar"],                    # Protection
    70:   ["Herald of the Sun", "Templar"],             # Retribution

    # Priest
    256:  ["Oracle", "Voidweaver"],                     # Discipline
    257:  ["Archon", "Oracle"],                         # Holy
    258:  ["Archon", "Voidweaver"],                     # Shadow

    # Rogue
    259:  ["Deathstalker", "Fatebound"],                # Assassination
    260:  ["Fatebound", "Trickster"],                   # Outlaw
    261:  ["Deathstalker", "Trickster"],                # Subtlety

    # Shaman
    262:  ["Farseer", "Stormbringer"],                  # Elemental
    263:  ["Stormbringer", "Totemic"],                  # Enhancement
    264:  ["Farseer", "Totemic"],                       # Restoration

    # Warlock
    265:  ["Hellcaller", "Soul Harvester"],             # Affliction
    266:  ["Diabolist", "Soul Harvester"],              # Demonology
    267:  ["Diabolist", "Hellcaller"],                  # Destruction

    # Warrior
    71:   ["Colossus", "Slayer"],                       # Arms
    72:   ["Mountain Thane", "Slayer"],                 # Fury
    73:   ["Colossus", "Mountain Thane"],               # Protection
}


def check_against(tree_names):
    """
    Compare this table against the names the client data actually has.

    Returns (unknown, unused): trees named here that traitsubtree does not know,
    and hero trees the client has that no spec here claims. Both are worth
    printing - the first means a typo or a rename, the second usually means a
    patch added a tree and this table has not caught up.
    """
    claimed = {tree for trees in SPEC_HERO_TREES.values() for tree in trees}
    unknown = sorted(claimed - set(tree_names))
    unused = sorted(set(tree_names) - claimed)
    return unknown, unused
