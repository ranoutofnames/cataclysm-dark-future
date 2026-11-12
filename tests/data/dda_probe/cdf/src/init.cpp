// Fixture type table for tools/cdf_dda_probe.py --selftest. Same registration form as CDF src/init.cpp.
void DynamicDataLoader::register_fixture_types()
{
    add( "GENERIC", &load_generic );
    add( "MOD_INFO", &load_ignored_type );
    add( "json_flag", &load_json_flag );
}
