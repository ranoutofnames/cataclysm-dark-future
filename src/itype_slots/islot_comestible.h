#pragma once

#include "itype.h"

struct islot_comestible {
    public:
        friend Item_factory;
        friend item;
        /** subtype, e.g. FOOD, DRINK, MED */
        std::string comesttype;

        /** tool needed to consume (e.g. lighter for cigarettes) */
        itype_id tool = itype_id::NULL_ID();

        /** Defaults # of charges (drugs, loaf of bread? etc) */
        int def_charges = 1;

        /** effect on character thirst (may be negative) */
        int quench = 0;

        /** Nutrition values to use for this type when they aren't calculated from
         * components */
        nutrients default_nutrition;

        /** Time until becomes rotten at standard temperature, or zero if never spoils */
        time_duration spoils = 0_turns;

        /** addiction potential */
        int addict = 0;

        /** effects of addiction */
        add_type add = add_type::NONE;

        /** stimulant effect */
        int stim = 0;

        /**fatigue altering effect*/
        int fatigue_mod = 0;

        /** Reference to other item that replaces this one as a component in recipe results */
        itype_id cooks_like;

        /** Reference to item that will be received after smoking current item */
        itype_id smoking_result;

        /** TODO: add documentation */
        int healthy = 0;

        /** chance (odds) of becoming parasitised when eating (zero if never occurs) */
        int parasites = 0;

        /**Amount of radiation you get from this comestible*/
        int radiation = 0;

        //pet food category
        std::set<std::string> petfood;

        /** freezing point in degrees Fahrenheit, below this temperature item can freeze */
        int freeze_point = units::to_fahrenheit( temperatures::freezing );

        /**List of diseases carried by this comestible and their associated probability*/
        std::map<diseasetype_id, int> contamination;

        //** specific heats in J/(g K) and latent heat in J/g */
        float specific_heat_liquid = 4.186;
        float specific_heat_solid = 2.108;
        float latent_heat = 333;

        /** A penalty applied to fun for every time this food has been eaten in the last 48 hours */
        int monotony_penalty = 2;

        /** 1 nutr ~= 8.7kcal (1 nutr/5min = 288 nutr/day at 2500kcal/day) */
        static constexpr float kcal_per_nutr = 2500.0f / ( 12 * 24 );

        bool has_calories() const {
            return default_nutrition.kcal > 0;
        }

        int get_default_nutr() const {
            return default_nutrition.kcal / kcal_per_nutr;
        }

        /** The monster group that is drawn from when the item rots away */
        mongroup_id rot_spawn = mongroup_id::NULL_ID();

        /** Chance the above monster group spawns*/
        int rot_spawn_chance = 10;

    private:
        /** effect on morale when consuming */
        int fun = 0;
};
