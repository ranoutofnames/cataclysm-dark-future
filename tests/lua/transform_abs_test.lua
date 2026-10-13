test_data.near_count = gapi.transform_radius_abs(test_data.transform_id, 1, test_data.near)
test_data.far_count = gapi.transform_radius_abs(test_data.transform_id, 1, test_data.far)
test_data.unknown_count = gapi.transform_radius_abs("no_such_transform", 1, test_data.near)
