#include "Engine/Landscape/FLandscapeSection.h"

namespace Engine {

    FLandscapeSection::FLandscapeSection(Uint32 X, Uint32 Y)
		: mX{ X },
		  mY{ Y } {
    }

    Uint32 FLandscapeSection::GetX() const {
        return mX;
    }

    Uint32 FLandscapeSection::GetY() const {
        return mY;
    }

}
