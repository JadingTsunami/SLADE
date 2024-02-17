
/*******************************************************************
 * SLADE - It's a Doom Editor
 * Copyright (C) 2008-2014 Simon Judd
 *
 * Email:       sirjuddington@gmail.com
 * Web:         http://slade.mancubus.net
 * Filename:    Edit2D.cpp
 * Description: Map Editor 2D modes (vertices/lines/etc) editing
 *              functionality
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *******************************************************************/


/*******************************************************************
 * INCLUDES
 *******************************************************************/
#include "Main.h"
#include "Edit2D.h"
#include "Game/Configuration.h"
#include "General/Clipboard.h"
#include "MapEditor/MapEditContext.h"
#include "MapEditor/SectorBuilder.h"
#include "MapEditor/UndoSteps.h"
#include "Utility/MathStuff.h"


/*******************************************************************
 * VARIABLES
 *******************************************************************/
CVAR(Bool, map_merge_lines_on_delete_vertex, false, CVAR_SAVE)
CVAR(Bool, map_remove_invalid_lines, false, CVAR_SAVE)


/*******************************************************************
 * EDIT2D CLASS FUNCTIONS
 *******************************************************************/

/* Edit2D::Edit2D
 * Edit2D class constructor
 *******************************************************************/
Edit2D::Edit2D(MapEditContext& context) :
	context_{ context },
	copy_line_{ nullptr, nullptr, &copy_side_front_, &copy_side_back_, nullptr }
{
}

/* Edit2D::mirror
 * Mirror selected objects horizontally or vertically depending on
 * [x_axis]
 *******************************************************************/
void Edit2D::mirror(bool x_axis) const
{
	using MapEditor::Mode;

	// Mirror things
	if (context_.editMode() == Mode::Things)
	{
		// Begin undo level
		context_.beginUndoRecord("Mirror Things", true, false, false);

		// Get things to mirror
		auto things = context_.selection().selectedThings();

		// Get midpoint
		bbox_t bbox;
		for (unsigned a = 0; a < things.size(); a++)
			bbox.extend(things[a]->xPos(), things[a]->yPos());

		// Mirror
		for (unsigned a = 0; a < things.size(); a++)
		{
			// Position
			if (x_axis)
				context_.map().moveThing(
					things[a]->getIndex(),
					bbox.mid_x() - (things[a]->xPos() - bbox.mid_x()),
					things[a]->yPos()
				);
			else
				context_.map().moveThing(
					things[a]->getIndex(),
					things[a]->xPos(),
					bbox.mid_y() - (things[a]->yPos() - bbox.mid_y())
				);

			// Direction
			int angle = things[a]->getAngle();
			if (x_axis)
			{
				angle += 90;
				angle = 360 - angle;
				angle -= 90;
			}
			else
				angle = 360 - angle;
			while (angle < 0)
				angle += 360;
			things[a]->setIntProperty("angle", angle);
		}
		context_.endUndoRecord(true);
	}

	// Mirror map architecture
	else if (context_.editMode() != Mode::Visual)
	{
		// Begin undo level
		context_.beginUndoRecord("Mirror Map Architecture", true, false, false);

		// Get vertices to mirror
		vector<MapVertex*> vertices;
		vector<MapLine*> lines;
		if (context_.editMode() == Mode::Vertices)
			vertices = context_.selection().selectedVertices();
		else if (context_.editMode() == Mode::Lines)
		{
			auto sel = context_.selection().selectedLines();
			for (auto line : sel)
			{
				VECTOR_ADD_UNIQUE(vertices, line->v1());
				VECTOR_ADD_UNIQUE(vertices, line->v2());
				lines.push_back(line);
			}
		}
		else if (context_.editMode() == Mode::Sectors)
		{
			auto sectors = context_.selection().selectedSectors();
			for (auto sector : sectors)
			{
				sector->getVertices(vertices);
				sector->getLines(lines);
			}
		}

		// Get midpoint
		bbox_t bbox;
		for (unsigned a = 0; a < vertices.size(); a++)
			bbox.extend(vertices[a]->xPos(), vertices[a]->yPos());

		// Mirror vertices
		for (unsigned a = 0; a < vertices.size(); a++)
		{
			// Position
			if (x_axis)
			{
				context_.map().moveVertex(
					vertices[a]->getIndex(),
					bbox.mid_x() - (vertices[a]->xPos() - bbox.mid_x()),
					vertices[a]->yPos()
				);
			}
			else
			{
				context_.map().moveVertex(
					vertices[a]->getIndex(),
					vertices[a]->xPos(),
					bbox.mid_y() - (vertices[a]->yPos() - bbox.mid_y())
				);
			}
		}

		// Flip lines (just swap vertices)
		for (unsigned a = 0; a < lines.size(); a++)
			lines[a]->flip(false);

		context_.endUndoRecord(true);
	}
}

/* Edit2D::editObjectProperties
 * Opens a dialog containing a MapObjectPropsPanel to edit properties
 * for all selected (or hilighted) objects
 *******************************************************************/
void Edit2D::editObjectProperties()
{
	auto selection = context_.selection().selectedObjects();
	if (selection.empty())
		return;

	// Begin recording undo level
	context_.beginUndoRecord(S_FMT("Property Edit (%s)", context_.modeString(false)));
	for (auto item : selection)
		context_.recordPropertyChangeUndoStep(item);

	bool done = MapEditor::editObjectProperties(selection);
	if (done)
	{
		context_.renderer().forceUpdate();
		context_.updateDisplay();

		if (context_.editMode() == MapEditor::Mode::Things)
			copyProperties(selection[0]);
	}

	// End undo level
	context_.endUndoRecord(done);
}

/* Edit2D::splitLine
 * Splits the line closest to [x,y] at the closest point on the line
 *******************************************************************/
void Edit2D::splitLine(double x, double y, double min_dist) const
{
	fpoint2_t point(x, y);

	// Get the closest line
	int lindex = context_.map().nearestLine(point, min_dist);
	auto line = context_.map().getLine(lindex);

	// Do nothing if no line is close enough
	if (!line)
		return;

	// Begin recording undo level
	context_.beginUndoRecord("Split Line", true, true, false);

	// Get closest point on the line
	auto closest = MathStuff::closestPointOnLine(point, line->seg());

    /* JDS: Respect snap to grid setting when splitting lines */
    closest.x = context_.snapToGrid(closest.x, false);
    closest.y = context_.snapToGrid(closest.y, false);

	// Create vertex there
	auto vertex = context_.map().createVertex(closest.x, closest.y);

	// Do line split
	context_.map().splitLine(line, vertex);

	// Finish recording undo level
	context_.endUndoRecord();
}

/* Edit2D::flipLines
 * Flips all selected lines, and sides if [sides] is true
 *******************************************************************/
void Edit2D::flipLines(bool sides) const
{
	// Get selected/hilighted line(s)
	auto lines = context_.selection().selectedLines();
	if (lines.empty())
		return;

	// Go through list
	context_.undoManager()->beginRecord("Flip Line");
	for (unsigned a = 0; a < lines.size(); a++)
	{
		context_.undoManager()->recordUndoStep(new MapEditor::PropertyChangeUS(lines[a]));
		lines[a]->flip(sides);
	}
	context_.undoManager()->endRecord(true);

	// Update display
	context_.updateDisplay();
}

/* Edit2D::correctLineSectors
 * Attempts to correct sector references on all selected lines
 *******************************************************************/
void Edit2D::correctLineSectors() const
{
	// Get selected/hilighted line(s)
	auto lines = context_.selection().selectedLines();
	if (lines.empty())
		return;

	context_.beginUndoRecord("Correct Line Sectors");

	bool changed = false;
	for (unsigned a = 0; a < lines.size(); a++)
	{
		if (context_.map().correctLineSectors(lines[a]))
			changed = true;
	}

	context_.endUndoRecord(changed);

	// Update display
	if (changed)
	{
		context_.addEditorMessage("Corrected Sector references");
		context_.updateDisplay();
	}
}

/* Edit2D::changeSectorHeight
 * Changes floor and/or ceiling heights on all selected sectors by
 * [amount]
 *******************************************************************/
void Edit2D::changeSectorHeight(int amount, bool floor, bool ceiling) const
{
	// Do nothing if not in sectors mode
	if (context_.editMode() != MapEditor::Mode::Sectors)
		return;

	// Get selected sectors (if any)
	auto selection = context_.selection().selectedSectors();
	if (selection.empty())
		return;

	// If we're modifying both heights, take sector_mode into account
	if (floor && ceiling)
	{
		if (context_.sectorEditMode() == MapEditor::SectorMode::Floor)
			ceiling = false;
		if (context_.sectorEditMode() == MapEditor::SectorMode::Ceiling)
			floor = false;
	}

	// Begin record undo level
	context_.beginUndoRecordLocked("Change Sector Height", true, false, false);

	// Go through selection
	for (unsigned a = 0; a < selection.size(); a++)
	{
		// Change floor height
		if (floor)
		{
			int height = selection[a]->intProperty("heightfloor");
			selection[a]->setIntProperty("heightfloor", height + amount);
		}

		// Change ceiling height
		if (ceiling)
		{
			int height = selection[a]->intProperty("heightceiling");
			selection[a]->setIntProperty("heightceiling", height + amount);
		}
	}

	// End record undo level
	context_.endUndoRecord();

	// Add editor message
	string what;
	if (floor && !ceiling)
		what = "Floor";
	else if (!floor && ceiling)
		what = "Ceiling";
	else
		what = "Floor and ceiling";
	string inc = "increased";
	if (amount < 0)
	{
		inc = "decreased";
		amount = -amount;
	}
	context_.addEditorMessage(S_FMT("%s height %s by %d", what, inc, amount));

	// Update display
	context_.updateDisplay();
}

/* Edit2D::changeSectorLight
 * Changes the light level for all selected sectors, increments if
 * [up] is true, decrements otherwise
 *******************************************************************/
void Edit2D::changeSectorLight(bool up, bool fine) const
{
	// Do nothing if not in sectors mode
	if (context_.editMode() != MapEditor::Mode::Sectors)
		return;

	// Get selected sectors (if any)
	auto selection = context_.selection().selectedSectors();
	if (selection.empty())
		return;

	// Begin record undo level
	context_.beginUndoRecordLocked("Change Sector Light", true, false, false);

	// Go through selection
	for (unsigned a = 0; a < selection.size(); a++)
	{
		// Get current light
		int light = selection[a]->intProperty("lightlevel");

		// Increment/decrement
		if (up)
			light = fine ? light + 1 : Game::configuration().upLightLevel(light);
		else
			light = fine ? light - 1 : Game::configuration().downLightLevel(light);

		// Change light level
		selection[a]->setIntProperty("lightlevel", light);
	}

	// End record undo level
	context_.endUndoRecord();

	// Add editor message
	//auto dir = up ? "increased" : "decreased";
	int amount = fine ? 1 : Game::configuration().lightLevelInterval();
	context_.addEditorMessage(S_FMT("Light level %s by %d", up ? "increased" : "decreased", amount));

	// Update display
	context_.updateDisplay();
}

/* Edit2D::changeSectorTexture
 * Depending on the current sector edit mode, either opens the
 * sector texture overlay (normal) or browses for the ceiling or
 * floor texture (ceiling/floor edit mode)
 *******************************************************************/
void Edit2D::changeSectorTexture() const
{
	using MapEditor::SectorMode;

	// Get selected sectors
	auto selection = context_.selection().selectedSectors();
	if (selection.empty())
		return;

	// Determine the initial texture
	string texture, browser_title, undo_name;
	if (context_.sectorEditMode() == SectorMode::Floor)
	{
		texture = selection[0]->stringProperty("texturefloor");
		browser_title = "Browse Floor Texture";
		undo_name = "Change Floor Texture";
	}
	else if (context_.sectorEditMode() == SectorMode::Ceiling)
	{
		texture = selection[0]->stringProperty("textureceiling");
		browser_title = "Browse Ceiling Texture";
		undo_name = "Change Ceiling Texture";
	}
	else
	{
		context_.openSectorTextureOverlay(selection);
		return;
	}

	// Lock hilight
	bool hl_lock = context_.selection().hilightLocked();
	context_.selection().lockHilight();

	// Open texture browser
	string selected_tex = MapEditor::browseTexture(texture, 1, context_.map(), browser_title);
	if (!selected_tex.empty())
	{
		// Set texture depending on edit mode
		context_.beginUndoRecord(undo_name, true, false, false);
		for (unsigned a = 0; a < selection.size(); a++)
		{
			if (context_.sectorEditMode() == SectorMode::Floor)
				selection[a]->setStringProperty("texturefloor", selected_tex);
			else if (context_.sectorEditMode() == SectorMode::Ceiling)
				selection[a]->setStringProperty("textureceiling", selected_tex);
		}
		context_.endUndoRecord();
	}

	// Unlock hilight if needed
	context_.selection().lockHilight(hl_lock);
	context_.renderer().renderer2D().clearTextureCache();
}

/* Edit2D::joinSectors
 * Joins all selected sectors. If [remove_lines] is true, all
 * resulting lines with both sides set to the joined sector are
 * removed
 *******************************************************************/
void Edit2D::joinSectors(bool remove_lines) const
{
	// Check edit mode
	if (context_.editMode() != MapEditor::Mode::Sectors)
		return;

	// Get sectors to merge
	auto sectors = context_.selection().selectedSectors(false);
	if (sectors.size() < 2) // Need at least 2 sectors to join
		return;

	// Get 'target' sector
	auto target = sectors[0];

	// Clear selection
	context_.selection().clear();

	// Init list of lines
	vector<MapLine*> lines;

	// Begin recording undo level
	context_.beginUndoRecord("Join/Merge Sectors", true, false, true);

	// Go through merge sectors
	for (unsigned a = 1; a < sectors.size(); a++)
	{
		// Go through sector sides
		auto sector = sectors[a];
		while (sector->connectedSides().size() > 0)
		{
			// Set sector
			auto side = sector->connectedSides()[0];
			side->setSector(target);

			// Add line to list if not already there
			bool exists = false;
			for (unsigned l = 0; l < lines.size(); l++)
			{
				if (side->getParentLine() == lines[l])
				{
					exists = true;
					break;
				}
			}
			if (!exists)
				lines.push_back(side->getParentLine());
		}

		// Delete sector
		context_.map().removeSector(sector);
	}

	// Remove any changed lines that now have the target sector on both sides (if needed)
	int nlines = 0;
	vector<MapVertex*> verts;
	if (remove_lines)
	{
		for (unsigned a = 0; a < lines.size(); a++)
		{
			if (lines[a]->frontSector() == target && lines[a]->backSector() == target)
			{
				VECTOR_ADD_UNIQUE(verts, lines[a]->v1());
				VECTOR_ADD_UNIQUE(verts, lines[a]->v2());
				context_.map().removeLine(lines[a]);
				nlines++;
			}
		}
	}

	// Remove any resulting detached vertices
	for (unsigned a = 0; a < verts.size(); a++)
	{
		if (verts[a]->nConnectedLines() == 0)
			context_.map().removeVertex(verts[a]);
	}

	// Finish recording undo level
	context_.endUndoRecord();

	// Editor message
	if (nlines == 0)
		context_.addEditorMessage(S_FMT("Joined %lu Sectors", sectors.size()));
	else
		context_.addEditorMessage(S_FMT("Joined %lu Sectors (removed %d Lines)", sectors.size(), nlines));
}

/* Edit2D::changeThingType
 * Opens the thing type browser for the currently selected thing(s)
 *******************************************************************/
void Edit2D::changeThingType() const
{
	// Get selected things (if any)
	auto selection = context_.selection().selectedThings();

	// Do nothing if no selection or hilight
	if (selection.size() == 0)
		return;

	// Browse thing type
	int newtype = MapEditor::browseThingType(selection[0]->getType(), context_.map());
	if (newtype >= 0)
	{
		// Go through selection
		context_.beginUndoRecord("Thing Type Change", true, false, false);
		for (unsigned a = 0; a < selection.size(); a++)
			selection[a]->setIntProperty("type", newtype);
		context_.endUndoRecord(true);

		// Add editor message
		string type_name = Game::configuration().thingType(newtype).name();
		if (selection.size() == 1)
			context_.addEditorMessage(S_FMT("Changed type to \"%s\"", type_name));
		else
			context_.addEditorMessage(S_FMT("Changed %lu things to type \"%s\"", selection.size(), type_name));

		// Update display
		context_.updateDisplay();
	}
}

/* Edit2D::thingQuickAngle
 * Sets the angle of all selected things to face toward [mouse_pos]
 *******************************************************************/
void Edit2D::thingQuickAngle(fpoint2_t mouse_pos) const
{
	// Do nothing if not in things mode
	if (context_.editMode() != MapEditor::Mode::Things)
		return;

	for (auto thing : context_.selection().selectedThings())
		thing->setAnglePoint(mouse_pos);
}

/* Edit2D::copy
 * Copies all selected objects
 *******************************************************************/
void Edit2D::copy() const
{
	using MapEditor::Mode;
	auto mode = context_.editMode();

	// Can't copy/paste vertices (no point)
	if (mode == Mode::Vertices)
	{
		//addEditorMessage("Copy/Paste not supported for vertices");
		return;
	}

	// Clear current clipboard contents
	theClipboard->clear();

	// Copy lines
	if (mode == Mode::Lines || mode == Mode::Sectors)
	{
		// Get selected lines
		vector<MapLine*> lines;
		if (mode == Mode::Lines)
			lines = context_.selection().selectedLines();
		else if (mode == Mode::Sectors)
		{
			for (auto sector : context_.selection().selectedSectors())
				sector->getLines(lines);
		}

		// Add to clipboard
		auto c = new MapArchClipboardItem();
		c->addLines(lines);
		theClipboard->addItem(c);

		// Editor message
		context_.addEditorMessage(S_FMT("Copied %s", c->getInfo()));
	}

	// Copy things
	else if (mode == Mode::Things)
	{
		// Get selected things
		auto things = context_.selection().selectedThings();

		// Add to clipboard
		auto c = new MapThingsClipboardItem();
		c->addThings(things);
		theClipboard->addItem(c);

		// Editor message
		context_.addEditorMessage(S_FMT("Copied %s", c->getInfo()));
	}
}


/* Edit2D::paste_rotate
 * Rotate pasted map architecture, centered on the midpoint.
 *******************************************************************/
void Edit2D::paste_rotate(double angle)
{
	// Go through clipboard items
	for (unsigned a = 0; a < theClipboard->nItems(); a++)
	{
		if (theClipboard->getItem(a)->getType() == CLIPBOARD_MAP_ARCH)
		{
			auto clip = (MapArchClipboardItem*)theClipboard->getItem(a);
            vector<MapVertex*> v = clip->vertices;

            bbox_t bbox;
            for(unsigned i = 0; i < v.size(); i++) {
                bbox.extend(v[i]->floatProperty("x"), v[i]->floatProperty("y"));
            }

            if(!bbox.is_valid()) continue;

            fpoint2_t midp = bbox.mid();


            for (auto& vertex : v) {
                fpoint2_t new_pos = MathStuff::rotatePoint(midp, vertex->point(), angle);
                vertex->setFloatProperty("x", new_pos.x);
                vertex->setFloatProperty("y", new_pos.y);
            }

            context_.addEditorMessage(S_FMT("Rotated pasted map architecture by %g", angle));
		}
		// Things
		else if (theClipboard->getItem(a)->getType() == CLIPBOARD_MAP_THINGS)
		{
            // TODO: Skip for now. In future, may want to re-align these
            // with pasted architecture.
            /*
             * // Rotate things
             * for (auto& thing : things_)
		     * thing.position = MathStuff::rotatePoint(mid, thing.old_position, rotation_);
             */
        }
    }
}

/* Edit2D::paste_resize
 * Resize pasted map architecture, centered on the midpoint.
 * Note this is NOT a scaling up/down!
 *******************************************************************/
void Edit2D::paste_resize(double resize_x, double resize_y)
{
	// Go through clipboard items
	for (unsigned a = 0; a < theClipboard->nItems(); a++)
	{
		if (theClipboard->getItem(a)->getType() == CLIPBOARD_MAP_ARCH)
		{
			auto clip = (MapArchClipboardItem*)theClipboard->getItem(a);
            vector<MapVertex*> v = clip->vertices;
            vector<MapVertex*> finished;
            typedef struct {
                MapVertex* vertex;
                double newX;
                double newY;
            } vertex_remap;
            vector<vertex_remap> remaps;

            vector<MapLine*> lines;
            clip->getLines(lines);

            if (!lines.empty()) {
                context_.addEditorMessage(S_FMT("Vector scaling applied."));
                /* Adapted from: https://stackoverflow.com/questions/54033808/how-to-offset-polygon-edges */
                double vnX, vnY, vpX, vpY;
                double nnnX, nnnY;
                double npnX, npnY;
                double bisX, bisY;
                for (MapLine* line : lines) {
                    /* find the partner line */
                    MapVertex* curr = line->vertex2;
                    MapVertex* prev = line->vertex1;
                    MapVertex* next = NULL;
                    for (MapLine* candidate : lines) {
                        if (candidate->vertex1 == curr) {
                            next = candidate->vertex2;
                            break;
                        }
                    }

                    if (!next) continue;

                    vnX = next->floatProperty("x") - curr->floatProperty("x");
                    vnY = next->floatProperty("y") - curr->floatProperty("y");
                    fpoint2_t vnn(vnX, vnY);
                    vnn.normalize();
                    nnnX = vnn.y;
                    nnnY = -vnn.x;

                    vpX = curr->floatProperty("x") - prev->floatProperty("x");
                    vpY = curr->floatProperty("y") - prev->floatProperty("y");
                    fpoint2_t vpn(vpX, vpY);
                    vpn.normalize();
                    npnX = vpn.y;
                    npnY = -vpn.x;

                    bisX = (nnnX + npnX);
                    bisY = (nnnY + npnY);

                    fpoint2_t bisn(bisX,bisY);
                    bisn.normalize();

                    remaps.push_back({curr, curr->floatProperty("x") - resize_x * bisn.x, curr->floatProperty("y") - resize_y * bisn.y });
                }

                for (vertex_remap r : remaps) {
                    MapVertex* c = r.vertex;
                    c->setFloatProperty("x", r.newX);
                    c->setFloatProperty("y", r.newY);
                }

            } else if (false) {
                context_.addEditorMessage(S_FMT("Polygonal line scaling applied."));
                for (auto& line : lines) {
                    /* circuit (polygonal) scaling */
                    /* walk through each identified circuit and scale its vertices accordingly */
                    /* fixup first/last */
                    MapVertex* c1 = line->vertex1;
                    MapVertex* c2 = line->vertex2;
                    double dx = c2->floatProperty("x") - c1->floatProperty("x");
                    double dy = c2->floatProperty("y") - c1->floatProperty("y");
                    double length = sqrt(dx*dx+dy*dy);
                    double delta_x = -dy/length;
                    double delta_y = dx/length;
                    c1->setFloatProperty("x", c1->floatProperty("x") + resize_x * delta_x * 0.5);
                    c1->setFloatProperty("y", c1->floatProperty("y") + resize_y * delta_y * 0.5);
                    c2->setFloatProperty("x", c2->floatProperty("x") + resize_x * delta_x * 0.5);
                    c2->setFloatProperty("y", c2->floatProperty("y") + resize_y * delta_y * 0.5);
                }
            } else {
                context_.addEditorMessage(S_FMT("Simple scaling applied."));
                vector<MapVertex*> v = clip->vertices;
                /* simple scaling algorithm */
                bbox_t bbox;
                for(unsigned i = 0; i < v.size(); i++) {
                    bbox.extend(v[i]->floatProperty("x"), v[i]->floatProperty("y"));
                }

                if(!bbox.is_valid()) continue;

                fpoint2_t midp = bbox.mid();

                double midx = midp.x;
                double midy = midp.y;

                for(unsigned i = 0; i < v.size(); i++) {
                    double xp = v[i]->floatProperty("x");
                    double yp = v[i]->floatProperty("y");
                    if (xp > midx) xp += resize_x;
                    else xp -= resize_x;
                    if (yp > midy) yp += resize_y;
                    else yp -= resize_y;
                    v[i]->setFloatProperty("x", xp);
                    v[i]->setFloatProperty("y", yp);
                }
            }
		}

		// Things
		else if (theClipboard->getItem(a)->getType() == CLIPBOARD_MAP_THINGS)
		{
            // TODO: Skip for now. In future, may want to re-align these
            // with pasted architecture.
        }
    }

}

/* Edit2D::paste
 * Pastes previously copied objects at [mouse_pos]
 *******************************************************************/
void Edit2D::paste(fpoint2_t mouse_pos) const
{
	// Go through clipboard items
	for (unsigned a = 0; a < theClipboard->nItems(); a++)
	{
		// Map architecture
		if (theClipboard->getItem(a)->getType() == CLIPBOARD_MAP_ARCH)
		{
			context_.beginUndoRecord("Paste Map Architecture");
			auto clip = (MapArchClipboardItem*)theClipboard->getItem(a);
			// Snap the geometry in such a way that it stays in the same
			// position relative to the grid
			auto pos = context_.relativeSnapToGrid(clip->getMidpoint(), mouse_pos);
			auto new_verts = clip->pasteToMap(&context_.map(), pos);
			context_.map().mergeArch(new_verts);
			context_.addEditorMessage(S_FMT("Pasted %s", clip->getInfo()));
			context_.endUndoRecord(true);
		}

		// Things
		else if (theClipboard->getItem(a)->getType() == CLIPBOARD_MAP_THINGS)
		{
			context_.beginUndoRecord("Paste Things", false, true, false);
			auto clip = (MapThingsClipboardItem*)theClipboard->getItem(a);
			// Snap the geometry in such a way that it stays in the same
			// position relative to the grid
			auto pos = context_.relativeSnapToGrid(clip->getMidpoint(), mouse_pos);
			clip->pasteToMap(&context_.map(), pos);
			context_.addEditorMessage(S_FMT("Pasted %s", clip->getInfo()));
			context_.endUndoRecord(true);
		}
	}
}

/* Edit2D::copyProperties
 * Copies the properties from [object] to be used for paste/create
 *******************************************************************/
void Edit2D::copyProperties(MapObject* object)
{
	auto selection = context_.selection();

	// Do nothing if no selection or hilight
	if (!selection.hasHilightOrSelection())
		return;

	// Sectors mode
	if (context_.editMode() == MapEditor::Mode::Sectors)
	{
		// Copy selection/hilight properties
		if (selection.size() > 0)
			copy_sector_.copy(context_.map().getSector(selection[0].index));
		else if (selection.hasHilight())
			copy_sector_.copy(selection.hilightedSector());

		// Editor message
		if (!object)
			context_.addEditorMessage("Copied sector properties");

		sector_copied_ = true;
	}

	// Things mode
	else if (context_.editMode() == MapEditor::Mode::Things)
	{
		// Copy given object properties (if any)
		if (object && object->getObjType() == MOBJ_THING)
			copy_thing_.copy(object);
		else
		{
			// Otherwise copy selection/hilight properties
			if (selection.size() > 0)
				copy_thing_.copy(context_.map().getThing(selection[0].index));
			else if (selection.hasHilight())
				copy_thing_.copy(selection.hilightedThing());
			else
				return;
		}

		// Editor message
		if (!object)
			context_.addEditorMessage("Copied thing properties");

		thing_copied_ = true;
	}

	// Lines mode
	else if (context_.editMode() == MapEditor::Mode::Lines)
	{
		if (selection.size() > 0)
			copy_line_.copy(context_.map().getLine(selection[0].index));
		else if (selection.hasHilight())
			copy_line_.copy(selection.hilightedLine());

		if (!object)
			context_.addEditorMessage("Copied line properties");

		line_copied_ = true;
	}
}

/* Edit2D::pasteProperties
 * Pastes previously copied properties to all selected objects
 *******************************************************************/
void Edit2D::pasteProperties()
{
	// Do nothing if no selection or hilight
	if (!context_.selection().hasHilightOrSelection())
		return;

	// Sectors mode
	if (context_.editMode() == MapEditor::Mode::Sectors)
	{
		// Do nothing if no properties have been copied
		if (!sector_copied_)
			return;

		// Paste properties to selection/hilight
		context_.beginUndoRecord("Paste Sector Properties", true, false, false);
		for (auto sector : context_.selection().selectedSectors())
			sector->copy(&copy_sector_);
		context_.endUndoRecord();

		// Editor message
		context_.addEditorMessage("Pasted sector properties");
	}

	// Things mode
	if (context_.editMode() == MapEditor::Mode::Things)
	{
		// Do nothing if no properties have been copied
		if (!thing_copied_)
			return;

		// Paste properties to selection/hilight
		context_.beginUndoRecord("Paste Thing Properties", true, false, false);
		for (auto thing : context_.selection().selectedThings())
		{
			// Paste properties (but keep position)
			double x = thing->xPos();
			double y = thing->yPos();
			thing->copy(&copy_thing_);
			thing->setFloatProperty("x", x);
			thing->setFloatProperty("y", y);
		}
		context_.endUndoRecord();

		// Editor message
		context_.addEditorMessage("Pasted thing properties");
	}

	// Lines mode
	else if (context_.editMode() == MapEditor::Mode::Lines)
	{
		// Do nothing if no properties have been copied
		if (!line_copied_)
			return;

		// Paste properties to selection/hilight
		context_.beginUndoRecord("Paste Line Properties", true, false, false);
		for (auto line : context_.selection().selectedLines())
			line->copy(&copy_line_);
		context_.endUndoRecord();

		// Editor message
		context_.addEditorMessage("Pasted line properties");
	}

	// Update display
	context_.updateDisplay();
}

/* Edit2D::createObject
 * Creates an object (depending on edit mode) at [x,y]
 *******************************************************************/
void Edit2D::createObject(double x, double y)
{
	using MapEditor::Mode;

	// Vertices mode
	if (context_.editMode() == Mode::Vertices)
	{
		// If there are less than 2 vertices currently selected, just create a vertex at x,y
		if (context_.selection().size() < 2)
			createVertex(x, y);
		else
		{
			// Otherwise, create lines between selected vertices
			context_.beginUndoRecord("Create Lines", false, true, false);
			auto vertices = context_.selection().selectedVertices(false);
			for (unsigned a = 0; a < vertices.size() - 1; a++)
				context_.map().createLine(vertices[a], vertices[a + 1]);
			context_.endUndoRecord(true);

			// Editor message
			context_.addEditorMessage(S_FMT("Created %lu line(s)", context_.selection().size() - 1));

			// Clear selection
			context_.selection().clear();
		}
	}

	// Sectors mode
	else if (context_.editMode() == Mode::Sectors)
	{
		// Sector
		if (context_.map().nLines() > 0)
		{
			createSector(x, y);
		}
		else
		{
			// Just create a vertex
			createVertex(x, y);
			context_.setEditMode(Mode::Lines);
		}
	}

	// Things mode
	else if (context_.editMode() == Mode::Things)
		createThing(x, y);
}

/* Edit2D::createVertex
 * Creates a new vertex at [x,y]
 *******************************************************************/
void Edit2D::createVertex(double x, double y) const
{
	// Snap coordinates to grid if necessary
	x = context_.snapToGrid(x, false);
	y = context_.snapToGrid(y, false);

	// Create vertex
	context_.beginUndoRecord("Create Vertex", true, true, false);
	auto vertex = context_.map().createVertex(x, y, 2);
	context_.endUndoRecord(true);

	// Editor message
	if (vertex)
		context_.addEditorMessage(S_FMT("Created vertex at (%d, %d)", (int)vertex->xPos(), (int)vertex->yPos()));
}

/* Edit2D::createThing
 * Creates a new thing at [x,y]
 *******************************************************************/
void Edit2D::createThing(double x, double y) const
{
	// Snap coordinates to grid if necessary
	x = context_.snapToGrid(x, false);
	y = context_.snapToGrid(y, false);

	// Begin undo step
	context_.beginUndoRecord("Create Thing", false, true, false);

	// Create thing
	auto thing = context_.map().createThing(x, y);

	// Setup properties
	Game::configuration().applyDefaults(thing, context_.map().currentFormat() == MAP_UDMF);
	if (thing_copied_ && thing)
	{
		// Copy type and angle from the last copied thing
		thing->setIntProperty("type", copy_thing_.getType());
		thing->setIntProperty("angle", copy_thing_.getAngle());
	}

	// End undo step
	context_.endUndoRecord(true);

	// Editor message
	if (thing)
		context_.addEditorMessage(S_FMT("Created thing at (%d, %d)", (int)thing->xPos(), (int)thing->yPos()));
}

/* Edit2D::createSector
 * Creates a new sector at [x,y]
 *******************************************************************/
void Edit2D::createSector(double x, double y) const
{
	fpoint2_t point(x, y);
	auto& map = context_.map();

	// Find nearest line
	int nearest = map.nearestLine(point, 99999999);
	auto line = map.getLine(nearest);
	if (!line)
		return;

	// Determine side
	double side = MathStuff::lineSide(point, line->seg());

	// Get sector to copy if we're in sectors mode
	MapSector* sector_copy = nullptr;
	if (context_.editMode() == MapEditor::Mode::Sectors && context_.selection().size() > 0)
		sector_copy = map.getSector(context_.selection().begin()->index);

	// Run sector builder
	SectorBuilder builder;
	bool ok;
	if (side >= 0)
		ok = builder.traceSector(&map, line, true);
	else
		ok = builder.traceSector(&map, line, false);

	// Do nothing if sector was already valid
	if (builder.isValidSector())
		return;

	// Create sector from builder result if needed
	if (ok)
	{
		context_.beginUndoRecord("Create Sector", true, true, false);
		builder.createSector(nullptr, sector_copy);

		// Flash
		context_.renderer().animateSelectionChange({ (int)map.nSectors() - 1, MapEditor::ItemType::Sector });
	}

	// Set some sector defaults from game configuration if needed
	if (!sector_copy && ok)
	{
		auto new_sector = map.getSector(map.nSectors() - 1);
		if (new_sector->getCeilingTex().IsEmpty())
			Game::configuration().applyDefaults(new_sector, map.currentFormat() == MAP_UDMF);
	}

	// Editor message
	if (ok)
	{
		context_.addEditorMessage(S_FMT("Created sector #%lu", map.nSectors() - 1));
		context_.endUndoRecord(true);
	}
	else
		context_.addEditorMessage("Sector creation failed: " + builder.getError());
}

/* Edit2D::deleteObject
 * Deletes all selected objects, depending on edit mode
 *******************************************************************/
void Edit2D::deleteObject() const
{
	using MapEditor::Mode;

	switch (context_.editMode())
	{
	case Mode::Vertices:	deleteVertex(); break;
	case Mode::Lines:		deleteLine(); break;
	case Mode::Sectors:		deleteSector(); break;
	case Mode::Things:		deleteThing(); break;
	default: return;
	}

	// Record undo step
	context_.endUndoRecord(true);
}

/* Edit2D::deleteVertex
 * Deletes all selected vertices
 *******************************************************************/
void Edit2D::deleteVertex() const
{
	// Get selected vertices
	auto verts = context_.selection().selectedVertices();
	int index = -1;
	if (verts.size() == 1)
		index = verts[0]->getIndex();

	// Clear hilight and selection
	context_.selection().clear();
	context_.selection().clearHilight();

	// Begin undo step
	context_.beginUndoRecord("Delete Vertices", map_merge_lines_on_delete_vertex, false, true);

	// Delete them (if any)
	for (unsigned a = 0; a < verts.size(); a++)
		context_.map().removeVertex(verts[a], map_merge_lines_on_delete_vertex);

	// Remove detached vertices
	context_.map().removeDetachedVertices();

	// Editor message
	if (verts.size() == 1)
		context_.addEditorMessage(S_FMT("Deleted vertex #%d", index));
	else if (verts.size() > 1)
		context_.addEditorMessage(S_FMT("Deleted %lu vertices", verts.size()));
}

/* Edit2D::deleteLine
 * Deletes all selected lines
 *******************************************************************/
void Edit2D::deleteLine() const
{
	// Get selected lines
	auto lines = context_.selection().selectedLines();
	int index = -1;
	if (lines.size() == 1)
		index = lines[0]->getIndex();

	// Clear hilight and selection
	context_.selection().clear();
	context_.selection().clearHilight();

	// Begin undo step
	context_.beginUndoRecord("Delete Lines", false, false, true);

	// Delete them (if any)
	for (unsigned a = 0; a < lines.size(); a++)
		context_.map().removeLine(lines[a]);

	// Remove detached vertices
	context_.map().removeDetachedVertices();

	// Editor message
	if (lines.size() == 1)
		context_.addEditorMessage(S_FMT("Deleted line #%d", index));
	else if (lines.size() > 1)
		context_.addEditorMessage(S_FMT("Deleted %lu lines", lines.size()));
}

/* Edit2D::deleteThing
 * Deletes all selected things
 *******************************************************************/
void Edit2D::deleteThing() const
{
	// Get selected things
	auto things = context_.selection().selectedThings();
	int index = -1;
	if (things.size() == 1)
		index = things[0]->getIndex();

	// Clear hilight and selection
	context_.selection().clear();
	context_.selection().clearHilight();

	// Begin undo step
	context_.beginUndoRecord("Delete Things", false, false, true);

	// Delete them (if any)
	for (unsigned a = 0; a < things.size(); a++)
		context_.map().removeThing(things[a]);

	// Editor message
	if (things.size() == 1)
		context_.addEditorMessage(S_FMT("Deleted thing #%d", index));
	else if (things.size() > 1)
		context_.addEditorMessage(S_FMT("Deleted %lu things", things.size()));
}

/* Edit2D::deleteSector
 * Deletes all selected sectors
 *******************************************************************/
void Edit2D::deleteSector() const
{
	// Get selected sectors
	auto sectors = context_.selection().selectedSectors();
	int index = -1;
	if (sectors.size() == 1)
		index = sectors[0]->getIndex();

	// Clear hilight and selection
	context_.selection().clear();
	context_.selection().clearHilight();

	// Begin undo step
	context_.beginUndoRecord("Delete Sectors", true, false, true);

	// Delete them (if any), and keep lists of connected lines and sides
	vector<MapSide*> connected_sides;
	vector<MapLine*> connected_lines;
	for (auto sector : sectors)
	{
		for (unsigned s = 0; s < sector->connectedSides().size(); s++)
			connected_sides.push_back(sector->connectedSides()[s]);
		sector->getLines(connected_lines);

		context_.map().removeSector(sector);
	}

	// Remove all connected sides
	for (auto side : connected_sides)
	{
		// Before removing the side, check if we should flip the line
		auto line = side->getParentLine();
		if (side == line->s1() && line->s2())
			line->flip();

		context_.map().removeSide(side);
	}

	// Remove resulting invalid lines
	if (map_remove_invalid_lines)
	{
		for (auto line : connected_lines)
		{
			if (!line->s1() && !line->s2())
				context_.map().removeLine(line);
		}
	}

	// Try to fill in textures on any lines that just became one-sided
	for (auto line : connected_lines)
	{
		MapSide* side;
		if (line->s1() && !line->s2())
			side = line->s1();
		else if (!line->s1() && line->s2())
			side = line->s2();
		else
			continue;

		if (side->getTexMiddle() != "-")
			continue;

		// Inherit textures from upper or lower
		if (side->getTexUpper() != "-")
			side->setStringProperty("texturemiddle", side->getTexUpper());
		else if (side->getTexLower() != "-")
			side->setStringProperty("texturemiddle", side->getTexLower());

		// Clear any existing textures, which are no longer visible
		side->setStringProperty("texturetop", "-");
		side->setStringProperty("texturebottom", "-");
	}

	// Editor message
	if (sectors.size() == 1)
		context_.addEditorMessage(S_FMT("Deleted sector #%d", index));
	else if (sectors.size() > 1)
		context_.addEditorMessage(S_FMT("Deleted %lu sector", sectors.size()));

	// Remove detached vertices
	context_.map().removeDetachedVertices();
}


/* Edit2D::makeDoor
 * Make a sector into a door
 *******************************************************************/
void Edit2D::makeDoor() const
{
	// Do nothing if not in sectors mode
	if (context_.editMode() != MapEditor::Mode::Sectors) {
        context_.addEditorMessage("Make door failed: Can only make doors in sectors mode.");
		return;
    }

	// Get selected sectors (if any)
	auto selection = context_.selection().selectedSectors();
	if (selection.size() != 1) {
        context_.addEditorMessage("Make door failed: Can only make one door at a time.");
		return;
    }

	// Begin record undo level
	context_.beginUndoRecordLocked("Make Door", true, false, false);

    // 1. Move the ceiling down to the floor
    int floorheight = selection[0]->intProperty("heightfloor");
    int ceilheight = selection[0]->intProperty("heightceiling");
    selection[0]->setIntProperty("heightceiling", floorheight);

    // 2. Set ceiling texture to FLAT20
    selection[0]->setStringProperty("textureceiling", "FLAT20");

	// Init list of lines
	vector<MapLine*> lines;
    selection[0]->getLines(lines);

    MapSector* inside = NULL;
    for( int i = 0; i < lines.size(); i++ ) {
        if(lines[i]->frontSector() && !lines[i]->backSector()) {
            inside = lines[i]->frontSector();
            break;
        }
    }

    for( int i = 0; i < lines.size(); i++ ) {
        if ( lines[i]->frontSector() && lines[i]->backSector() ) {
            // 3. Flip all 2-sided linedefs pointing inward so they face outward
            if( lines[i]->frontSector() == inside || !inside )
                lines[i]->flip();

            // 3. Set all 2-sided linedef upper textures to BIGDOOR2, the STARTAN of doors
            lines[i]->s1()->setStringProperty("texturetop", "BIGDOOR2");
            // 4. Set all 2-sided linedef specials to 1 (DR DOOR)
            lines[i]->setIntProperty("special", 1);

        } else {

            MapSide* side;
            if (lines[i]->s1() && !lines[i]->s2())
                side = lines[i]->s1();
            else if (!lines[i]->s1() && lines[i]->s2())
                side = lines[i]->s2();
            else
                continue;

            // 5. Set all 1-sided linedef textures to DOORTRAK
            side->setStringProperty("texturemiddle", "DOORTRAK");
            // 6. Set all 1-sided linedef to lower unpegged
			Game::configuration().setLineBasicFlag(
				"dontpegbottom",
				lines[i],
				context_.map().currentFormat(),
				true
			);

        }

    }

	// End record undo level
	context_.endUndoRecord();

	// Add editor message
	context_.addEditorMessage("Made sector into a door.");

	// Update display
	context_.updateDisplay();
}


/* Edit2D::curveLine
 * Curve a linedef
 *******************************************************************/
void Edit2D::curveLines(bool concave) const
{
	// Do nothing if not in lines mode
	if (context_.editMode() != MapEditor::Mode::Lines) {
        context_.addEditorMessage("Curve Linedef only works in Linedef edit mode.");
		return;
    }

	// Begin record undo level
	context_.beginUndoRecord("Curve linedef", true, true, true);

    auto sel = context_.selection().selectedLines();
    for (auto line : sel)
    {
        /* calculate line midpoint */
        double x1 = concave ? line->x1() : line->x2();
        double x2 = concave ? line->x2() : line->x1();
        double y1 = concave ? line->y1() : line->y2();
        double y2 = concave ? line->y2() : line->y1();
        fpoint2_t center(x1 + ((x2 - x1) * 0.5), y1 + ((y2 - y1) * 0.5));

        double angle1 = atan2(y1 - center.y, x1 - center.x);
        double angle2 = atan2(y2 - center.y, x2 - center.x);

        angle1 += angle1 < 0 ? 2*PI : 0;
        angle2 += angle2 < 0 ? 2*PI : 0;

        /* FIXME: Allow user-input segments and radius */
        double radius = line->getLength() / 2;
        int segments = MAX(7,(radius)/8);

        double arc_angle = angle1 - angle2;
        if (arc_angle < 0) {
            arc_angle += 2*PI;
        }

        double segment_angle = -arc_angle / segments;
        for (int i = 1; i < segments; i++) {
            double a = angle1 + segment_angle * (concave ? segments - i : i);
            fpoint2_t v(center.x + cos(a)*radius, center.y + sin(a)*radius);
            auto vertex = context_.map().createVertex(v.x, v.y);
            context_.map().splitLine(line, vertex);
        }
    }

	// End record undo level
	context_.endUndoRecord();

}

/* Edit2D::bevelLines
 * Apply a beveled edge to 2 linedefs
 *******************************************************************/
void Edit2D::bevelLines() const
{
	// Do nothing if not in lines mode
	if (context_.editMode() != MapEditor::Mode::Lines) {
        context_.addEditorMessage("Bevel Linedef only works in Linedef edit mode.");
		return;
    }

    auto sel = context_.selection().selectedLines();

	if (sel.size() < 2) {
        context_.addEditorMessage("At least 2 linedefs must be selected for bevel mode.");
		return;
    }

    int pairs = 0;
    for (int i = 1; i < sel.size(); i++) {
        if ((sel[i-1]->point2() == sel[i]->point1())
                ||
           (sel[i-1]->point1() == sel[i]->point2())) {
            pairs++;
        }

    }

    if (pairs < sel.size()/2) {
        context_.addEditorMessage("Beveled linedefs must be selected in pairs and share a vertex.");
		return;
    }

    // Begin record undo level
    context_.beginUndoRecord("Bevel linedefs", true, true, true);

    for (int i = 1; i < sel.size(); i++) {
        // Beveled edges must share a start and end point
        MapLine* l0 = nullptr;
        MapLine* l1 = nullptr;

        if (sel[i-1]->point2() == sel[i]->point1()) {
            l0 = sel[i-1];
            l1 = sel[i];
        } else if (sel[i-1]->point1() == sel[i]->point2()) {
            l0 = sel[i];
            l1 = sel[i-1];
        } else {
            // not a pair, skip ahead
            continue;
        }

        /* adapted from:
         * https://stackoverflow.com/questions/44855794/html5-canvas-triangle-with-rounded-corners/44856925#44856925
         */

        fpoint2_t origin = l0->point2();
        fpoint2_t p1 = l0->point1();
        fpoint2_t p2 = l1->point2();
        fpoint2_t v1 = p1 - origin;
        fpoint2_t v2 = p2 - origin;
        fpoint2_t v1n = v1.normalized();
        fpoint2_t v2n = v2.normalized();

        double v1len = sqrt(v1.x * v1.x + v1.y * v1.y);
        double v2len = sqrt(v2.x * v2.x + v2.y * v2.y);
        double v1ang = atan2(v1n.y, v1n.x);
        double v2ang = atan2(v2n.y, v2n.x);

        double radius = min(l0->getLength(),l1->getLength());
        double sinA = v1n.x * v2n.y - v1n.y * v2n.x;
        double sinA90 = v1n.x *v2n.x - v1n.y * (-v2n.y);
        double angle = asin(MathStuff::clamp(sinA,-1,1));
        int direction = 1;

        if (sinA90 < 0) {
            if (angle < 0) {
                angle += PI;
            } else {
                angle = PI - angle;
                direction = -1;
            }
        } else if (angle > 0) {
            direction = -1;
        }

        double lenOut = fabs(cos(angle/2)*radius/sin(angle/2));

        fpoint2_t center;
        double angle1, angle2;

        center.x = origin.x + v2n.x * lenOut;
        center.y = origin.y + v2n.y * lenOut;

        center.x += -v2n.y * radius * direction;
        center.y += v2n.x * radius * direction;

        if (direction < 0) {
            angle1 = v1ang - PI / 2;
            angle2 = v2ang + PI / 2;
        } else {
            angle1 = v2ang - PI / 2;
            angle2 = v1ang + PI / 2;
        }

        /* TODO: Segment count could be configurable */
        int segments = MAX(3,(radius)/10);
        double bevel_angle = angle1 - angle2;
        if (bevel_angle < 0) {
            bevel_angle += 2*PI;
        }


        double segment_angle = -bevel_angle / segments;
        for (int i = 1; i < segments; i++) {
            double a = angle1 + segment_angle * (direction < 0 ? segments - i : i);
            fpoint2_t v(center.x + cos(a)*radius, center.y + sin(a)*radius);
            auto vertex = context_.map().createVertex(v.x, v.y);
            context_.map().splitLine((direction < 0 ? l1 : l0), vertex);
        }
        /* move endpoint vertex to new position */
        context_.map().moveVertex(
                (direction < 0 ? l0->v2Index() : l1->v1Index()),
                center.x + cos(angle1)*radius,
                center.y + sin(angle1)*radius);
    }

	// End record undo level
	context_.endUndoRecord();
}
