#pragma once

#include "MapEditor/SLADEMap/MapLine.h"
#include "MapEditor/SLADEMap/MapThing.h"
#include "MapEditor/SLADEMap/MapSector.h"
#include "MapEditor/SLADEMap/MapSide.h"

class MapObject;
class MapEditContext;

class Edit2D
{
public:
	Edit2D(MapEditContext& context);

	// General Editing
	void	mirror(bool x_axis) const;
	void	mirror_paste(bool x_axis) const;
	void 	editObjectProperties();

	// Lines
	void	splitLine(double x, double y, double min_dist = 64) const;
	void	flipLines(bool sides = true) const;
    void    curveLines(bool concave = false) const;
    void    bevelLines() const;
    void    copyFrontBackTexture() const;
	void	correctLineSectors() const;

	// Sectors
	void	changeSectorHeight(int amount, bool floor = true, bool ceiling = true) const;
	void	changeSectorLight(bool up, bool fine) const;
	void 	changeSectorTexture() const;
	void	joinSectors(bool remove_lines) const;
    void    makeDoor() const;

	// Things
	void	changeThingType() const;
	void	thingQuickAngle(fpoint2_t mouse_pos) const;

	// Copy / Paste
	void	copy() const;
	void	paste(fpoint2_t mouse_pos) const;
	void	copyProperties(MapObject* object = nullptr);
	void	pasteProperties();
	void	paste_resize(double resize_x, double resize_y);
	void	paste_rotate(double angle);

	// Create / Delete
	void	createObject(double x, double y);
	void	createVertex(double x, double y) const;
	void	createThing(double x, double y) const;
	void	createSector(double x, double y) const;
	void	deleteObject() const;
	void	deleteVertex() const;
	void	deleteLine() const;
	void	deleteThing() const;
	void	deleteSector() const;

private:
	MapEditContext&	context_;

	// Object properties and copy/paste
	MapThing	copy_thing_;
	MapSector	copy_sector_;
	MapSide		copy_side_front_;
	MapSide		copy_side_back_;
	MapLine		copy_line_;
	bool		line_copied_	= false;
	bool		sector_copied_	= false;
	bool		thing_copied_	= false;
};
