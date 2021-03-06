 /*
 * Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
 * Copyright (C) 2006-2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* ScriptData
SDName: Azuremyst_Isle
SD%Complete: 75
SDComment: Quest support: 9283, 9537, 9582, 9554, ? (special flight path, proper model for mount missing). Injured Draenei cosmetic only, 9582.
SDCategory: Azuremyst Isle
EndScriptData */

/* ContentData
npc_draenei_survivor
npc_engineer_spark_overgrind
npc_injured_draenei
npc_magwin
go_ravager_cage
npc_death_ravager
EndContentData */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "TemporarySummon.h"

/*######
## npc_draenei_survivor
######*/

enum draeneiSurvivor
{
    SAY_THANK_FOR_HEAL     = 0,
    SAY_ASK_FOR_HELP       = 1,
    SPELL_IRRIDATION       = 35046,
    SPELL_STUNNED          = 28630,
    EVENT_CAN_ASK_FOR_HELP = 1,
    EVENT_THANK_PLAYER     = 2,
    EVENT_RUN_AWAY         = 3
};

Position const CrashSite = { -4115.25f, -13754.75f };

class npc_draenei_survivor : public CreatureScript
{
public:
    npc_draenei_survivor() : CreatureScript("npc_draenei_survivor") { }

    struct npc_draenei_survivorAI : public ScriptedAI
    {
        npc_draenei_survivorAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            _playerGUID.Clear();
            _canAskForHelp = true;
            _canUpdateEvents = false;
            _tappedBySpell = false;
        }

        void Reset() override
        {
            Initialize();
            _events.Reset();

            DoCastSelf(SPELL_IRRIDATION, true);

            me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);
            me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);
            me->SetHealth(me->CountPctFromMaxHealth(10));
            me->SetStandState(UNIT_STAND_STATE_SLEEP);
        }

        void EnterCombat(Unit* /*who*/) override { }

        void MoveInLineOfSight(Unit* who) override
        {
            if (_canAskForHelp && who->GetTypeId() == TYPEID_PLAYER && me->IsFriendlyTo(who) && me->IsWithinDistInMap(who, 25.0f))
            {
                //Random switch between 4 texts
                Talk(SAY_ASK_FOR_HELP);

                _events.ScheduleEvent(EVENT_CAN_ASK_FOR_HELP, Seconds(16), Seconds(20));
                _canAskForHelp = false;
                _canUpdateEvents = true;
            }
        }

        void SpellHit(Unit* caster, const SpellInfo* spell) override
        {
            if (spell->SpellFamilyFlags[2] & 0x80000000 && !_tappedBySpell)
            {
                _events.Reset();
                _tappedBySpell = true;
                _canAskForHelp = false;
                _canUpdateEvents = true;

                me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);
                me->SetStandState(UNIT_STAND_STATE_STAND);

                _playerGUID = caster->GetGUID();
                if (Player* player = caster->ToPlayer())
                    player->KilledMonsterCredit(me->GetEntry());

                me->SetFacingToObject(caster);
                DoCastSelf(SPELL_STUNNED, true);
                _events.ScheduleEvent(EVENT_THANK_PLAYER, Seconds(4));
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (!_canUpdateEvents)
                return;

            _events.Update(diff);

            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_CAN_ASK_FOR_HELP:
                        _canAskForHelp = true;
                        _canUpdateEvents = false;
                        break;
                    case EVENT_THANK_PLAYER:
                        me->RemoveAurasDueToSpell(SPELL_IRRIDATION);
                        if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGUID))
                            Talk(SAY_THANK_FOR_HEAL, player);
                        _events.ScheduleEvent(EVENT_RUN_AWAY, Seconds(10));
                        break;
                    case EVENT_RUN_AWAY:
                        me->GetMotionMaster()->Clear();
                        me->GetMotionMaster()->MovePoint(0, me->GetPositionX() + (std::cos(me->GetAngle(CrashSite)) * 28.0f), me->GetPositionY() + (std::sin(me->GetAngle(CrashSite)) * 28.0f), me->GetPositionZ() + 1.0f);
                        me->DespawnOrUnsummon(Seconds(4));
                        break;
                    default:
                        break;
                }
            }
        }

    private:
        EventMap _events;
        bool _canUpdateEvents;
        bool _tappedBySpell;
        bool _canAskForHelp;
        ObjectGuid _playerGUID;
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_draenei_survivorAI(creature);
    }
};

/*######
## npc_engineer_spark_overgrind
######*/

enum Overgrind
{
    SAY_TEXT        = 0,
    SAY_EMOTE       = 1,
    ATTACK_YELL     = 2,

    AREA_COVE       = 3579,
    AREA_ISLE       = 3639,
    QUEST_GNOMERCY  = 9537,
    FACTION_HOSTILE = 14,
    SPELL_DYNAMITE  = 7978
};

class npc_engineer_spark_overgrind : public CreatureScript
{
public:
    npc_engineer_spark_overgrind() : CreatureScript("npc_engineer_spark_overgrind") { }

    struct npc_engineer_spark_overgrindAI : public ScriptedAI
    {
        npc_engineer_spark_overgrindAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            NormFaction = creature->getFaction();
            NpcFlags = creature->GetUInt32Value(UNIT_NPC_FLAGS);
        }

        void Initialize()
        {
            DynamiteTimer = 8000;
            EmoteTimer = urand(120000, 150000);

            if (me->GetAreaId() == AREA_COVE || me->GetAreaId() == AREA_ISLE)
                IsTreeEvent = true;
            else
                IsTreeEvent = false;
        }

        void Reset() override
        {
            Initialize();

            me->setFaction(NormFaction);
            me->SetUInt32Value(UNIT_NPC_FLAGS, NpcFlags);
        }

        void EnterCombat(Unit* who) override
        {
            Talk(ATTACK_YELL, who);
        }

        void sGossipSelect(Player* player, uint32 /*menuId*/, uint32 /*gossipListId*/) override
        {
            CloseGossipMenuFor(player);
            me->setFaction(FACTION_HOSTILE);
            me->Attack(player, true);
        }

        void UpdateAI(uint32 diff) override
        {
            if (!me->IsInCombat() && !IsTreeEvent)
            {
                if (EmoteTimer <= diff)
                {
                    Talk(SAY_TEXT);
                    Talk(SAY_EMOTE);
                    EmoteTimer = urand(120000, 150000);
                } else EmoteTimer -= diff;
            }
            else if (IsTreeEvent)
                return;

            if (!UpdateVictim())
                return;

            if (DynamiteTimer <= diff)
            {
                DoCastVictim(SPELL_DYNAMITE);
                DynamiteTimer = 8000;
            } else DynamiteTimer -= diff;

            DoMeleeAttackIfReady();
        }

    private:
        uint32 NormFaction;
        uint32 NpcFlags;
        uint32 DynamiteTimer;
        uint32 EmoteTimer;
        bool   IsTreeEvent;
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_engineer_spark_overgrindAI(creature);
    }
};

/*######
## npc_injured_draenei
######*/

class npc_injured_draenei : public CreatureScript
{
public:
    npc_injured_draenei() : CreatureScript("npc_injured_draenei") { }

    struct npc_injured_draeneiAI : public ScriptedAI
    {
        npc_injured_draeneiAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);
            me->SetHealth(me->CountPctFromMaxHealth(15));
            switch (urand(0, 1))
            {
                case 0:
                    me->SetStandState(UNIT_STAND_STATE_SIT);
                    break;

                case 1:
                    me->SetStandState(UNIT_STAND_STATE_SLEEP);
                    break;
            }
        }

        void EnterCombat(Unit* /*who*/) override { }

        void MoveInLineOfSight(Unit* /*who*/) override { }

        void UpdateAI(uint32 /*diff*/) override { }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_injured_draeneiAI(creature);
    }
};

/*######
## PlayerScript for quest 9531 "Tree's Company"
######*/
class PlayerScript_start_conversation_under_tree : public PlayerScript {
public:
    PlayerScript_start_conversation_under_tree() : PlayerScript("PlayerScript_start_conversation_under_tree") {}

    enum Geezle {
        QUEST_TREES_COMPANY = 9531,
        SPELL_TREE_DISGUISE = 30298,
        NPC_GEEZLE = 17318,
        DATA_START_EVENT = 27,
        AREA_TRAITORS_CAVE = 3579,
    };

    uint32 checkTimer = 1000;

    void OnUpdate(Player* player, uint32 diff) override {
        if (checkTimer <= diff) {
            if (player->GetAreaId() == AREA_TRAITORS_CAVE &&
                player->GetQuestStatus(QUEST_TREES_COMPANY) == QUEST_STATUS_INCOMPLETE &&
                player->HasAura(SPELL_TREE_DISGUISE)
                ) {
                if (Creature* Geezle = player->FindNearestCreature(NPC_GEEZLE, 50.0f, true)) {
                    Geezle->AI()->SetData(DATA_START_EVENT, DATA_START_EVENT);
                }
            }

            checkTimer = 1000;
        }
        else checkTimer -= diff;
    }
};

/*######
## npc geezle 17318 for quest 9531 "Tree's Company"
######*/
class npc_geezle : public CreatureScript {
public:
    npc_geezle() : CreatureScript("npc_geezle_17318") { }

    enum Geezle {
        QUEST_TREES_COMPANY = 9531,
        SPELL_TREE_DISGUISE = 30298,
        GEEZLE_SAY_1 = 0,
        SPARK_SAY_2 = 3,
        SPARK_SAY_3 = 4,
        GEEZLE_SAY_4 = 1,
        SPARK_SAY_5 = 5,
        SPARK_SAY_6 = 6,
        GEEZLE_SAY_7 = 2,
        EMOTE_SPARK = 7,
        NPC_SPARK = 17243,
        GO_NAGA_FLAG = 181694,
        EVENT_START_ANIM = 100,
        SPELL_DEATH_INVIS = 117555,
        DATA_START_EVENT = 27,
    };

    struct npc_geezle_AI : public ScriptedAI {
        npc_geezle_AI(Creature* creature) : ScriptedAI(creature) {
            Initialize();
        }

        void Initialize() {
            SparkGUID = ObjectGuid::Empty;
            EventStarted = false;
        }

        void Reset() override {
            _events.Reset();
            Initialize();
        }

        void StartEvent() {
            EventStarted = true;
            if (Creature* Spark = me->SummonCreature(NPC_SPARK, -5029.91f, -11291.79f, 8.096f, 0.0f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 1000)) {
                SparkGUID = Spark->GetGUID();
                Spark->setActive(true);
                Spark->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
            }
            _events.ScheduleEvent(EVENT_START_ANIM, 8000);
        }

        void CompleteQuest() {
            std::list<Player*> players;
            me->GetPlayerListInGrid(players, me->GetVisibilityRange());
            for (std::list<Player*>::const_iterator itr = players.begin(); itr != players.end(); ++itr) {
                if ((*itr)->ToPlayer()->GetQuestStatus(QUEST_TREES_COMPANY) == QUEST_STATUS_INCOMPLETE &&
                    (*itr)->ToPlayer()->HasAura(SPELL_TREE_DISGUISE)) {
                    (*itr)->KilledMonsterCredit(NPC_SPARK, ObjectGuid::Empty);
                }
            }
        }

        void UpdateAI(uint32 diff) override {

            _events.Update(diff);

            while (uint32 eventId = _events.ExecuteEvent()) {
                if (Creature* Spark = me->FindNearestCreature(NPC_SPARK, me->GetVisibilityRange(), true)) {
                    switch (eventId) {
                    case EVENT_START_ANIM:
                        me->SetWalk(true);
                        Spark->SetWalk(true);
                        Spark->GetMotionMaster()->MovePoint(0, -5080.70f, -11253.61f, 0.56f);
                        me->GetMotionMaster()->MovePoint(0, -5092.26f, -11252, 0.71f);
                        _events.ScheduleEvent(EVENT_START_ANIM + 1, 9000);
                        break;
                    case EVENT_START_ANIM + 1:
                        Spark->AI()->Talk(EMOTE_SPARK);
                        _events.ScheduleEvent(EVENT_START_ANIM + 2, 1000);
                        break;
                    case EVENT_START_ANIM + 2:
                        Talk(GEEZLE_SAY_1, Spark);
                        Spark->SetInFront(me);
                        me->SetInFront(Spark);
                        _events.ScheduleEvent(EVENT_START_ANIM + 3, 5000);
                        break;
                    case EVENT_START_ANIM + 3:
                        Spark->AI()->Talk(SPARK_SAY_2);
                        _events.ScheduleEvent(EVENT_START_ANIM + 4, 7000);
                        break;
                    case EVENT_START_ANIM + 4:
                        Spark->AI()->Talk(SPARK_SAY_3);
                        _events.ScheduleEvent(EVENT_START_ANIM + 5, 8000);
                        break;
                    case EVENT_START_ANIM + 5:
                        Talk(GEEZLE_SAY_4, Spark);
                        _events.ScheduleEvent(EVENT_START_ANIM + 6, 9000);
                        break;
                    case EVENT_START_ANIM + 6:
                        Spark->AI()->Talk(SPARK_SAY_5);
                        _events.ScheduleEvent(EVENT_START_ANIM + 7, 8000);
                        break;
                    case EVENT_START_ANIM + 7:
                        Spark->AI()->Talk(SPARK_SAY_6);
                        _events.ScheduleEvent(EVENT_START_ANIM + 8, 9000);
                        break;
                    case EVENT_START_ANIM + 8:
                        Talk(GEEZLE_SAY_7, Spark);
                        _events.ScheduleEvent(EVENT_START_ANIM + 9, 2000);
                        break;
                    case EVENT_START_ANIM + 9:
                        me->GetMotionMaster()->MoveTargetedHome();
                        Spark->GetMotionMaster()->MovePoint(0, -5029.91f, -11291.79f, 8.096f, 0.0f);
                        CompleteQuest();
                        _events.ScheduleEvent(EVENT_START_ANIM + 10, 9000);
                        break;
                    case EVENT_START_ANIM + 10:
                        DoCastSelf(SPELL_DEATH_INVIS, true);
                        Spark->DespawnOrUnsummon(5000, Seconds(20));
                        me->DespawnOrUnsummon(5000, Seconds(20));
                        EventStarted = false;
                        break;
                    default:
                        break;
                    }
                }
            }
            DoMeleeAttackIfReady();
        }

        void SetData(uint32 id, uint32 /*value*/) override {
            switch (id) {
            case DATA_START_EVENT:
                if (!EventStarted) {
                    StartEvent();
                }
                break;
            }
        }

    private:
        EventMap _events;
        ObjectGuid SparkGUID;
        bool EventStarted;
    };

    CreatureAI* GetAI(Creature* creature) const override {
        return new npc_geezle_AI(creature);
    }
};

/*######
## npc magwin 17312 for quest 9528 "A Cry For Help"
######*/
class npc_magwin : public CreatureScript {
public:
    npc_magwin() : CreatureScript("npc_magwin_17312") { }

    enum Magwin {
        QUEST_A_CRY_FOR_HELP = 9528,
        SAY_START = 0,
        SAY_AGGRO = 1,
        SAY_PROGRESS = 2,
        SAY_END1 = 3,
        SAY_END2 = 4,
        EMOTE_HUG = 5,
        FACTION_QUEST = 113
    };

    struct npc_magwin_AI : public npc_escortAI {
        npc_magwin_AI(Creature* creature) : npc_escortAI(creature) { }

        void Reset() override { }

        void EnterCombat(Unit* who) override {
            Talk(SAY_AGGRO, who);
        }

        void sQuestAccept(Player* player, Quest const* quest) override {
            if (quest->GetQuestId() == QUEST_A_CRY_FOR_HELP) {
                me->setFaction(FACTION_QUEST);
                npc_escortAI::Start(true, false, player->GetGUID());
            }
        }

        void WaypointReached(uint32 waypointId) override {
            if (Player* player = GetPlayerForEscort()) {
                switch (waypointId) {
                case 0:
                    Talk(SAY_START, player);
                    break;
                case 17:
                    Talk(SAY_PROGRESS, player);
                    break;
                case 28:
                    Talk(SAY_END1, player);
                    break;
                case 29:
                    Talk(EMOTE_HUG, player);
                    Talk(SAY_END2, player);
                    player->GroupEventHappens(QUEST_A_CRY_FOR_HELP, me);
                    break;
                }
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override {
        return new npc_magwin_AI(creature);
    }
};

enum RavagerCage
{
    NPC_DEATH_RAVAGER       = 17556,

    SPELL_REND              = 13443,
    SPELL_ENRAGING_BITE     = 30736,

    QUEST_STRENGTH_ONE      = 9582
};

class go_ravager_cage : public GameObjectScript
{
public:
    go_ravager_cage() : GameObjectScript("go_ravager_cage") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        go->UseDoorOrButton();
        if (player->GetQuestStatus(QUEST_STRENGTH_ONE) == QUEST_STATUS_INCOMPLETE)
        {
            if (Creature* ravager = go->FindNearestCreature(NPC_DEATH_RAVAGER, 5.0f, true))
            {
                ravager->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
                ravager->SetReactState(REACT_AGGRESSIVE);
                ravager->AI()->AttackStart(player);
            }
        }
        return true;
    }
};

class npc_death_ravager : public CreatureScript
{
public:
    npc_death_ravager() : CreatureScript("npc_death_ravager") { }

    struct npc_death_ravagerAI : public ScriptedAI
    {
        npc_death_ravagerAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            RendTimer = 30000;
            EnragingBiteTimer = 20000;
        }

        uint32 RendTimer;
        uint32 EnragingBiteTimer;

        void Reset() override
        {
            Initialize();

            me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
            me->SetReactState(REACT_PASSIVE);
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            if (RendTimer <= diff)
            {
                DoCastVictim(SPELL_REND);
                RendTimer = 30000;
            }
            else RendTimer -= diff;

            if (EnragingBiteTimer <= diff)
            {
                DoCastVictim(SPELL_ENRAGING_BITE);
                EnragingBiteTimer = 15000;
            }
            else EnragingBiteTimer -= diff;

            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_death_ravagerAI(creature);
    }
};

/*########
## Quest: The Prophecy of Akida
########*/

enum BristlelimbCage
{
    QUEST_THE_PROPHECY_OF_AKIDA         = 9544,
    NPC_STILLPINE_CAPITIVE              = 17375,
    GO_BRISTELIMB_CAGE                  = 181714,

    CAPITIVE_SAY                        = 0,

    POINT_INIT                          = 1,
    EVENT_DESPAWN                       = 1,
};

class npc_stillpine_capitive : public CreatureScript
{
    public:
        npc_stillpine_capitive() : CreatureScript("npc_stillpine_capitive") { }

        struct npc_stillpine_capitiveAI : public ScriptedAI
        {
            npc_stillpine_capitiveAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
            }

            void Initialize()
            {
                _playerGUID.Clear();
                _movementComplete = false;
            }

            void Reset() override
            {
                if (GameObject* cage = me->FindNearestGameObject(GO_BRISTELIMB_CAGE, 5.0f))
                {
                    cage->SetLootState(GO_JUST_DEACTIVATED);
                    cage->SetGoState(GO_STATE_READY);
                }
                _events.Reset();
                Initialize();
            }

            void StartMoving(Player* owner)
            {
                if (owner)
                {
                    Talk(CAPITIVE_SAY, owner);
                    _playerGUID = owner->GetGUID();
                }
                Position pos = me->GetNearPosition(3.0f, 0.0f);
                me->GetMotionMaster()->MovePoint(POINT_INIT, pos);
            }

            void MovementInform(uint32 type, uint32 id) override
            {
                if (type != POINT_MOTION_TYPE || id != POINT_INIT)
                    return;

                if (Player* _player = ObjectAccessor::GetPlayer(*me, _playerGUID))
                    _player->KilledMonsterCredit(me->GetEntry(), me->GetGUID());

                _movementComplete = true;
                _events.ScheduleEvent(EVENT_DESPAWN, 3500);
            }

            void UpdateAI(uint32 diff) override
            {
                if (!_movementComplete)
                    return;

                _events.Update(diff);

                if (_events.ExecuteEvent() == EVENT_DESPAWN)
                    me->DespawnOrUnsummon();
            }

        private:
            ObjectGuid _playerGUID;
            EventMap _events;
            bool _movementComplete;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_stillpine_capitiveAI(creature);
        }
};

class go_bristlelimb_cage : public GameObjectScript
{
    public:
        go_bristlelimb_cage() : GameObjectScript("go_bristlelimb_cage") { }

        bool OnGossipHello(Player* player, GameObject* go) override
        {
            go->SetGoState(GO_STATE_READY);
            if (player->GetQuestStatus(QUEST_THE_PROPHECY_OF_AKIDA) == QUEST_STATUS_INCOMPLETE)
            {
                if (Creature* capitive = go->FindNearestCreature(NPC_STILLPINE_CAPITIVE, 5.0f, true))
                {
                    go->ResetDoorOrButton();
                    ENSURE_AI(npc_stillpine_capitive::npc_stillpine_capitiveAI, capitive->AI())->StartMoving(player);
                    return false;
                }
            }
            return true;
        }
};

/*######
## AddSC
######*/
void AddSC_azuremyst_isle()
{
    new npc_draenei_survivor();
    new npc_engineer_spark_overgrind();
    new npc_injured_draenei();
    new npc_death_ravager();
    new go_ravager_cage();
    new npc_stillpine_capitive();
    new go_bristlelimb_cage();
    new npc_geezle();
    new npc_magwin();
    new PlayerScript_start_conversation_under_tree();
}
