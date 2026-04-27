import pytest
utils = pytest.importorskip("example_transcoding.utils")
image_u8 = utils.image_u8
color_quad_u8 = utils.color_quad_u8
pixel_coord = utils.pixel_coord

class TestUtils:
    def test_flood_fill_stack_overflow_protection(self):
        # Create a large image
        img = image_u8(1000, 1000)
        
        # Fill the entire image with a background color
        bg_color = color_quad_u8(0, 0, 0, 255)
        img.set_all(bg_color)
        
        # Try to flood fill with a small region that would cause stack overflow
        # This should not cause a crash or infinite loop
        fill_color = color_quad_u8(255, 255, 255, 255)
        b_color = color_quad_u8(0, 0, 0, 255)
        
        # Test with a point that would trigger large stack growth
        result = img.flood_fill(500, 500, fill_color, b_color, None)
        
        # Flood fill a large region; this should complete without stack overflow
        # and should not cause a crash or infinite loop
        fill_color = color_quad_u8(255, 255, 255, 255)
        b_color = color_quad_u8(0, 0, 0, 255)
        
        # Test with a point that triggers flood fill of the large background region
        result = img.flood_fill(500, 500, fill_color, b_color, None)
        
        # Should return a positive number indicating pixels filled
        assert result > 0
        
    def test_flood_fill_normal_operation(self):
        # Create a small image
        img = image_u8(10, 10)
        
        # Fill with background
        bg_color = color_quad_u8(0, 0, 0, 255)
        for y in range(10):
            for x in range(10):
                img.set_pixel_clipped(x, y, bg_color)
        
        # Fill a small region
        fill_color = color_quad_u8(255, 255, 255, 255)
        b_color = color_quad_u8(0, 0, 0, 255)
        
        # Test normal flood fill operation
        result = img.flood_fill(5, 5, fill_color, b_color, None)
        
        # Should return a positive number
        assert result > 0
        
    def test_flood_fill_edge_case_outside_bounds(self):
        # Create a small image
        img = image_u8(10, 10)
        
        # Fill with background
        bg_color = color_quad_u8(0, 0, 0, 255)
        for y in range(10):
            for x in range(10):
                img.set_pixel_clipped(x, y, bg_color)
        
        # Try to flood fill outside image bounds
        fill_color = color_quad_u8(255, 255, 255, 255)
        b_color = color_quad_u8(0, 0, 0, 255)
        
        # Should return 0 for out of bounds
        result = img.flood_fill(-1, 5, fill_color, b_color, None)
        assert result == 0
        
        result = img.flood_fill(5, -1, fill_color, b_color, None)
        assert result == 0
        
        result = img.flood_fill(15, 5, fill_color, b_color, None)
        assert result == 0
        
        result = img.flood_fill(5, 15, fill_color, b_color, None)
        assert result == 0