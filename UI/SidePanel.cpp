#include "SidePanel.h"

#include "CUI_Wnd.h"
#include "CUIControls.h"
#include "GGDrawUtil.h"
#include "GGStaticGraphic.h"
#include "GGDynamicGraphic.h"
#include "GGThreeButtonDlg.h"

#include "../client/human/HumanClientApp.h"
#include "../util/MultiplayerCommon.h"
#include "../universe/Predicates.h"
#include "../universe/ShipDesign.h"
#include "../util/Random.h"

#include "XMLDoc.h"
#include "GGBase.h"

#ifndef __GGScroll_h_
#include "GGScroll.h"
#endif

#include "../universe/Fleet.h"
#include "../universe/Ship.h"
#include "../util/OptionsDB.h"

#include "../UI/FleetWindow.h"

#include <boost/format.hpp>

#include "MapWnd.h"

using boost::lexical_cast;

#define ROTATING_PLANET_IMAGES 1 // set this to 1 to use the OpenGL-rendered rotating planets code

namespace {
    bool PlaySounds() {return GetOptionsDB().Get<bool>("UI.sound.enabled");}
    void PlaySidePanelOpenSound() {if (PlaySounds()) HumanClientApp::GetApp()->PlaySound(ClientUI::SoundDir() + GetOptionsDB().Get<std::string>("UI.sound.sidepanel-open"));}
    void PlayFarmingFocusClickSound() {if (PlaySounds()) HumanClientApp::GetApp()->PlaySound(ClientUI::SoundDir() + GetOptionsDB().Get<std::string>("UI.sound.farming-focus"));}
    void PlayIndustryFocusClickSound() {if (PlaySounds()) HumanClientApp::GetApp()->PlaySound(ClientUI::SoundDir() + GetOptionsDB().Get<std::string>("UI.sound.industry-focus"));}
    void PlayResearchFocusClickSound() {if (PlaySounds()) HumanClientApp::GetApp()->PlaySound(ClientUI::SoundDir() + GetOptionsDB().Get<std::string>("UI.sound.research-focus"));}
    void PlayMiningFocusClickSound() {if (PlaySounds()) HumanClientApp::GetApp()->PlaySound(ClientUI::SoundDir() + GetOptionsDB().Get<std::string>("UI.sound.mining-focus"));}
    void PlayTradeFocusClickSound() {if (PlaySounds()) HumanClientApp::GetApp()->PlaySound(ClientUI::SoundDir() + GetOptionsDB().Get<std::string>("UI.sound.trade-focus"));}
    void PlayBalancedFocusClickSound() {if (PlaySounds()) HumanClientApp::GetApp()->PlaySound(ClientUI::SoundDir() + GetOptionsDB().Get<std::string>("UI.sound.balanced-focus"));}

    const int MAX_PLANET_DIAMETER = 128; // size of a huge planet, in on-screen pixels
    const int MIN_PLANET_DIAMETER = MAX_PLANET_DIAMETER / 3; // size of a tiny planet, in on-screen pixels

    int CircleXFromY(double y, double r) {return static_cast<int>(std::sqrt(r * r - y * y) + 0.5);}

    struct RotatingPlanetData
    {
        RotatingPlanetData(const GG::XMLElement& elem)
        {
            if (elem.Tag() != "RotatingPlanetData")
                throw std::invalid_argument("Attempted to construct a RotatingPlanetData from an XMLElement that had a tag other than \"RotatingPlanetData\"");

            planet_type = lexical_cast<PlanetType>(elem.Child("planet_type").Text());
            filename = elem.Child("filename").Text();
            RPM = lexical_cast<double>(elem.Child("RPM").Text());
            axis_angle = lexical_cast<double>(elem.Child("axis_angle").Text());
            shininess = lexical_cast<double>(elem.Child("shininess").Text());

            shininess = std::max(0.0, std::min(shininess, 128.0)); // ensure proper bounds
        }

        GG::XMLElement XMLEncode() const
        {
            GG::XMLElement retval("RotatingPlanetData");
            retval.AppendChild(GG::XMLElement("planet_type", lexical_cast<std::string>(planet_type)));
            retval.AppendChild(GG::XMLElement("filename", filename));
            retval.AppendChild(GG::XMLElement("RPM", lexical_cast<std::string>(RPM)));
            retval.AppendChild(GG::XMLElement("axis_angle", lexical_cast<std::string>(axis_angle)));
            retval.AppendChild(GG::XMLElement("shininess", lexical_cast<std::string>(shininess)));
            return retval;
        }

        PlanetType planet_type; ///< the type of planet for which this data may be used
        std::string filename;   ///< the filename of the image used to texture a rotating image
        double RPM;             ///< the rotation of this planet, in revolutions per minute (may be negative, which will cause CW rotation)
        double axis_angle;      ///< the angle, in degrees, of the axis on which the planet rotates, measured CCW from straight up (may be negative)
        double shininess;       ///< the exponent of specular (shiny) reflection off of the planet; must be in [0.0, 128.0]
    };

    const std::map<PlanetType, std::vector<RotatingPlanetData> >& GetRotatingPlanetData()
    {
        static std::map<PlanetType, std::vector<RotatingPlanetData> > data;
        if (data.empty()) {
            GG::XMLDoc doc;
            std::ifstream ifs((ClientUI::ART_DIR + "planets/planets.xml").c_str());
            doc.ReadDoc(ifs);
            ifs.close();

            if (doc.root_node.ContainsChild("GLPlanets")) {
                const GG::XMLElement& elem = doc.root_node.Child("GLPlanets");
                for (GG::XMLElement::const_child_iterator it = elem.child_begin(); it != elem.child_end(); ++it) {
                    if (it->Tag() == "RotatingPlanetData") {
                        RotatingPlanetData current_data(*it);
                        data[current_data.planet_type].push_back(current_data);
                    }
                }
            }
        }
        return data;
    }

    double GetRotatingPlanetAmbientIntensity()
    {
        static double retval = -1.0;

        if (retval == -1.0) {
            GG::XMLDoc doc;
            std::ifstream ifs((ClientUI::ART_DIR + "planets/planets.xml").c_str());
            doc.ReadDoc(ifs);
            ifs.close();

            if (doc.root_node.ContainsChild("GLPlanets") && doc.root_node.Child("GLPlanets").ContainsChild("ambient_intensity"))
                retval = lexical_cast<double>(doc.root_node.Child("GLPlanets").Child("ambient_intensity").Text());
            else
                retval = 0.5;

            retval = std::max(0.0, std::min(retval, 1.0));
        }

        return retval;
    }

    double GetRotatingPlanetDiffuseIntensity()
    {
        static double retval = -1.0;

        if (retval == -1.0) {
            GG::XMLDoc doc;
            std::ifstream ifs((ClientUI::ART_DIR + "planets/planets.xml").c_str());
            doc.ReadDoc(ifs);
            ifs.close();

            if (doc.root_node.ContainsChild("GLPlanets") && doc.root_node.Child("GLPlanets").ContainsChild("diffuse_intensity"))
                retval = lexical_cast<double>(doc.root_node.Child("GLPlanets").Child("diffuse_intensity").Text());
            else
                retval = 0.5;

            retval = std::max(0.0, std::min(retval, 1.0));
        }

        return retval;
    }

    void RenderSphere(double r, const GG::Clr& ambient, const GG::Clr& diffuse, const GG::Clr& spec, double shine, 
                      boost::shared_ptr<GG::Texture> texture)
    {
        static GLUquadric* quad = gluNewQuadric();

        if (quad) {
            if (texture) {
                glBindTexture(GL_TEXTURE_2D, texture->OpenGLId());
            }

            if (shine) {
                glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, static_cast<float>(shine));
                GLfloat spec_v[] = {spec.r / 255.0, spec.g / 255.0, spec.b / 255.0, spec.a / 255.0};
                glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec_v);
            }
            GLfloat ambient_v[] = {ambient.r / 255.0, ambient.g / 255.0, ambient.b / 255.0, ambient.a / 255.0};
            glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient_v);
            GLfloat diffuse_v[] = {diffuse.r / 255.0, diffuse.g / 255.0, diffuse.b / 255.0, diffuse.a / 255.0};
            glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse_v);
            gluQuadricTexture(quad, texture ? GL_TRUE : GL_FALSE);
            gluQuadricNormals(quad, GLU_SMOOTH);
            gluQuadricOrientation(quad, GLU_OUTSIDE);

            glColor4ubv(GG::CLR_WHITE.v);
            gluSphere(quad, r, 100, 100);
        }
    }

    GLfloat* GetLightPosition()
    {
        static GLfloat retval[] = {0.0, 0.0, 0.0, 0.0};

        if (retval[0] == 0.0 && retval[1] == 0.0 && retval[2] == 0.0) {
            GG::XMLDoc doc;
            std::ifstream ifs((ClientUI::ART_DIR + "planets/planets.xml").c_str());
            doc.ReadDoc(ifs);
            ifs.close();

            retval[0] = lexical_cast<double>(doc.root_node.Child("GLPlanets").Child("light_pos").Child("x").Text());
            retval[1] = lexical_cast<double>(doc.root_node.Child("GLPlanets").Child("light_pos").Child("y").Text());
            retval[2] = lexical_cast<double>(doc.root_node.Child("GLPlanets").Child("light_pos").Child("z").Text());
        }

        return retval;
    }

    const std::map<StarType, std::vector<float> >& GetStarLightColors()
    {
        static std::map<StarType, std::vector<float> > light_colors;

        if (light_colors.empty()) {
            GG::XMLDoc doc;
            std::ifstream ifs((ClientUI::ART_DIR + "planets/planets.xml").c_str());
            doc.ReadDoc(ifs);
            ifs.close();

            if (doc.root_node.ContainsChild("GLStars") && 0 < doc.root_node.Child("GLStars").NumChildren()) {
                for (GG::XMLElement::child_iterator it = doc.root_node.Child("GLStars").child_begin(); it != doc.root_node.Child("GLStars").child_end(); ++it) {
                    std::vector<float>& color_vec = light_colors[lexical_cast<StarType>(it->Child("star_type").Text())];
                    GG::Clr color(it->Child("GG::Clr"));
                    color_vec.push_back(color.r / 255.0);
                    color_vec.push_back(color.g / 255.0);
                    color_vec.push_back(color.b / 255.0);
                    color_vec.push_back(color.a / 255.0);
                }
            } else {
                for (int i = STAR_BLUE; i < NUM_STAR_TYPES; ++i) {
                    light_colors[StarType(i)].resize(4, 1.0);
                }
            }
        }

        return light_colors;
    }

    void RenderPlanet(const GG::Pt& center, int diameter, boost::shared_ptr<GG::Texture> texture, double RPM, double axis_tilt, double shininess, StarType star_type)
    {
        HumanClientApp::GetApp()->Exit2DMode();

        // slide the texture coords to simulate a rotating axis
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glTranslated(GG::App::GetApp()->Ticks() / 1000.0 * RPM * 1.0 / 60.0, 0, 0.0);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, HumanClientApp::GetApp()->AppWidth(), HumanClientApp::GetApp()->AppHeight(), 0.0, 0.0, HumanClientApp::GetApp()->AppWidth());

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslated(center.x, center.y, -(diameter / 2 + 1));
        glRotated(100.0, -1.0, 0.0, 0.0); // make the poles upright, instead of head-on (we go a bit more than 90 degrees, to avoid some artifacting caused by the GLU-supplied texture coords)
        glRotated(axis_tilt, 0.0, 1.0, 0.0);  // axis tilt

        glPushAttrib(GL_LIGHTING_BIT | GL_ENABLE_BIT);
        GLfloat* light_position = GetLightPosition();
        //GLfloat light_position[] = {1.5, -0.8, -1.0, 0.0};
        glLightfv(GL_LIGHT0, GL_POSITION, light_position);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        const std::map<StarType, std::vector<float> >& star_light_colors = GetStarLightColors();
        glLightfv(GL_LIGHT0, GL_DIFFUSE, &star_light_colors.find(star_type)->second[0]);
        glLightfv(GL_LIGHT0, GL_SPECULAR, &star_light_colors.find(star_type)->second[0]);
        glEnable(GL_TEXTURE_2D);

        double intensity = GetRotatingPlanetAmbientIntensity();
        GG::Clr ambient(intensity, intensity, intensity, 1.0);
        intensity = GetRotatingPlanetDiffuseIntensity();
        GG::Clr diffuse(intensity, intensity, intensity, 1.0);
        RenderSphere(diameter / 2, ambient, diffuse, GG::CLR_WHITE, shininess, texture);

        glPopAttrib();

        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);

        HumanClientApp::GetApp()->Enter2DMode();
    }

    int PlanetDiameter(PlanetSize size)
    {
        double scale = 0.0;
        switch (size)
        {
        case SZ_TINY      : scale = 0.0/5.0; break;
        case SZ_SMALL     : scale = 1.0/5.0; break;
        case SZ_MEDIUM    : scale = 2.0/5.0; break;
        case SZ_LARGE     : scale = 3.0/5.0; break;
        case SZ_HUGE      : scale = 4.0/5.0; break;
        case SZ_GASGIANT  : scale = 5.0/5.0; break;
        case SZ_ASTEROIDS : scale = 5.0/5.0; break;
        default                   : scale = 2.0/5.0; break;
        }

        return static_cast<int>(MIN_PLANET_DIAMETER + (MAX_PLANET_DIAMETER - MIN_PLANET_DIAMETER) * scale);
    }

    bool temp_header_bool = RecordHeaderFile(SidePanelRevision());
    bool temp_source_bool = RecordSourceFile("$RCSfile$", "$Revision$");
}

class RotatingPlanetControl : public GG::Control
{
public:
    RotatingPlanetControl(int x, int y, PlanetSize size, StarType star_type, const RotatingPlanetData& planet_data) :
        GG::Control(x, y, PlanetDiameter(size), PlanetDiameter(size), 0),
        m_planet_data(planet_data),
        m_size(size),
        m_texture(GG::App::GetApp()->GetTexture(ClientUI::ART_DIR + m_planet_data.filename)),
        m_star_type(star_type)
    {
    }

    virtual bool Render()
    {
        RenderPlanet(UpperLeft() + GG::Pt(Width() / 2, Height() / 2), Width(), m_texture, 
                     SizeRotationFactor(m_size) * m_planet_data.RPM, m_planet_data.axis_angle, m_planet_data.shininess, m_star_type);
        return true;
    }

    void SetRotatingPlanetData(const RotatingPlanetData& planet_data)
    {
        m_planet_data = planet_data;
        m_texture = GG::App::GetApp()->GetTexture(m_planet_data.filename);
    }

private:
    double SizeRotationFactor(PlanetSize size) const
    {
        switch (size) {
        case SZ_TINY:     return 2.0;
        case SZ_SMALL:    return 1.5;
        case SZ_MEDIUM:   return 1.0;
        case SZ_LARGE:    return 0.75;
        case SZ_HUGE:     return 0.5;
        case SZ_GASGIANT: return 0.25;
        default:          return 1.0;
        }
        return 1.0;
    }

    RotatingPlanetData              m_planet_data;
    PlanetSize                      m_size;
    boost::shared_ptr<GG::Texture>  m_texture;
    StarType                        m_star_type;
};

////////////////////////////////////////////////
// SidePanel::PlanetPanel
////////////////////////////////////////////////
namespace {
  const int IMAGES_PER_PLANET_TYPE = 3; // number of planet images available per planet type (named "type1.png", "type2.png", ...)
  const int SYSTEM_NAME_FONT_SIZE = static_cast<int>(ClientUI::PTS*1.4);
  
  boost::shared_ptr<GG::Texture> GetTexture(const std::string& name, bool mipmap = false)
  {
    try
    {
      return HumanClientApp::GetApp()->GetTexture(name,mipmap);
    }
    catch(...)
    {
      return HumanClientApp::GetApp()->GetTexture(ClientUI::ART_DIR + "misc/missing.png",mipmap);
    }
  }

  boost::shared_ptr<GG::Texture> IconBalance   () {return GetTexture(ClientUI::ART_DIR + "icons/focusbuttoncrossicon.png"        );}
  boost::shared_ptr<GG::Texture> IconPopulation() {return GetTexture(ClientUI::ART_DIR + "icons/pop.png"        );}
  boost::shared_ptr<GG::Texture> IconIndustry  () {return GetTexture(ClientUI::ART_DIR + "icons/industry.png"   );}
  boost::shared_ptr<GG::Texture> IconResearch  () {return GetTexture(ClientUI::ART_DIR + "icons/research.png"   );}
  boost::shared_ptr<GG::Texture> IconMining    () {return GetTexture(ClientUI::ART_DIR + "icons/mining.png"     );}
  boost::shared_ptr<GG::Texture> IconFarming   () {return GetTexture(ClientUI::ART_DIR + "icons/farming.png"    );}
  boost::shared_ptr<GG::Texture> IconDefense   () {return GetTexture(ClientUI::ART_DIR + "icons/defensebase.png");}

  struct SystemRow : public GG::ListBox::Row
  {
    public:
      SystemRow(int system_id) : m_system_id(system_id) {data_type = "SystemID";}

      int m_system_id;
  };

  boost::shared_ptr<GG::Texture> GetPlanetTextureStatic(const Planet &planet)
  {
    std::string planet_image = ClientUI::ART_DIR + "planets/";
    switch (planet.Type())
    {
      case PT_SWAMP     : planet_image += "swamp"     ; break;
      case PT_TOXIC     : planet_image += "toxic"     ; break;
      case PT_INFERNO   : planet_image += "inferno"   ; break;
      case PT_RADIATED  : planet_image += "radiated"  ; break;
      case PT_BARREN    : planet_image += "barren"    ; break;
      case PT_TUNDRA    : planet_image += "tundra"    ; break;
      case PT_DESERT    : planet_image += "desert"    ; break;
      case PT_TERRAN    : planet_image += "terran"    ; break;
      case PT_OCEAN     : planet_image += "ocean"     ; break;
      case PT_GAIA      : planet_image += "gaia"      ; break;
      case PT_ASTEROIDS : planet_image += "asteroids" ; break;
      case PT_GASGIANT  : planet_image += "gasgiant"  ; break;    
      default           : planet_image += "barren"    ; break;
    }
    planet_image += lexical_cast<std::string>((planet.ID() % IMAGES_PER_PLANET_TYPE) + 1) + ".png";

    try
    {
      return HumanClientApp::GetApp()->GetTexture(planet_image);
    }
    catch(...)
    {
      return GetTexture(ClientUI::ART_DIR + "planets/terran1.png");
    }
  }

  GG::XMLElement GetXMLChild(GG::XMLElement &node,const std::string &child_path)
  {
    int index;

    if(-1==(index=child_path.find_first_of('.')))
      return node.ContainsChild(child_path)?node.Child(child_path):GG::XMLElement();
    else
      return node.ContainsChild(child_path.substr(0,index))
              ?GetXMLChild(node.Child(child_path.substr(0,index)),child_path.substr(index+1,child_path.length()-index-1))
              :GG::XMLElement();
  }

  std::string GetPlanetArtNodeName(const Planet &planet)
  {
    switch (planet.Type())
    {
      case PT_SWAMP     : return "Swamp"     ;
      case PT_TOXIC     : return "Toxic"     ;
      case PT_INFERNO   : return "Inferno"   ;
      case PT_RADIATED  : return "Radiated"  ;
      case PT_BARREN    : return "Barren"    ;
      case PT_TUNDRA    : return "Tundra"    ;
      case PT_DESERT    : return "Desert"    ;
      case PT_TERRAN    : return "Terran"    ;
      case PT_OCEAN     : return "Ocean"     ;
      case PT_GAIA      : return "Gaia"      ;
      case PT_ASTEROIDS : return "Asteroids" ;
      case PT_GASGIANT  : return "Gasgiant"  ;
      default           : return "Barren"    ;
    }
  }

  int GetPlanetTexturesDynamic(const std::string &node_name,const PlanetSize &planet_size,int art_variation,std::vector<boost::shared_ptr<GG::Texture> > &textures, int &start_frame, double &fps)
  {
    GG::XMLDoc planetart_doc;
    std::ifstream ifs((ClientUI::ART_DIR + "planets/planets.xml").c_str());
    planetart_doc.ReadDoc(ifs);
    ifs.close();

    std::string plt_art_node_name = "ArtPlanets."+node_name;

    GG::XMLElement plt_art_node = GetXMLChild(planetart_doc.root_node,plt_art_node_name);

    if(plt_art_node.Tag().length()>0)
    {
      std::vector<GG::XMLElement> plt_art_vec;

      for(int i=0; i< plt_art_node.NumChildren();i++)
      {
        GG::XMLElement single_plt_art =plt_art_node.Child(i);
        if(0==single_plt_art.Tag().compare("Art"))
          plt_art_vec.push_back(single_plt_art);
      }

      if(plt_art_vec.size()>0)
      {
        GG::XMLElement chosen_plt_art = plt_art_vec[art_variation % plt_art_vec.size()];
       
        if(chosen_plt_art.ContainsChild("File"))
        {
          GG::XMLElement file(chosen_plt_art.Child("File"));

          std::string filename = file.ContainsAttribute("Filename")?file.Attribute("Filename"):"";
          int from = file.ContainsAttribute("From")?lexical_cast<int>(file.Attribute("From")):0,
              to   = file.ContainsAttribute("To"  )?lexical_cast<int>(file.Attribute("To"  )):0;

          if(std::string::npos==filename.find('%'))
            textures.push_back(HumanClientApp::GetApp()->GetTexture(ClientUI::ART_DIR + "planets/"+filename));
          else
          {
            if(from<=to)
              for(int i=from; i<=to; i++)
              {
                std::string filename_image = filename,
                            index          = lexical_cast<std::string>(i);

                filename_image.replace(filename_image.find_last_of('%')-index.length()+1,index.length(),index);
                while(filename_image.find('%')!=std::string::npos)
                  filename_image.replace(filename_image.find('%'),1,"0");

                textures.push_back(HumanClientApp::GetApp()->GetTexture(ClientUI::ART_DIR + "planets/"+filename_image));
              }
          }
          if(chosen_plt_art.ContainsChild("FPS"))
          {
            std::string plt_size_name;
            switch(planet_size)
            {
              case SZ_TINY      : plt_size_name = "Tiny"     ; break;
              case SZ_SMALL     : plt_size_name = "Small"    ; break;
              case SZ_MEDIUM    : plt_size_name = "Medium"   ; break;
              case SZ_LARGE     : plt_size_name = "Large"    ; break;
              case SZ_HUGE      : plt_size_name = "Huge"     ; break;
              case SZ_ASTEROIDS : plt_size_name = "Asteroids"; break;
              case SZ_GASGIANT  : plt_size_name = "Gasgigant"; break;
              default                   : plt_size_name = "Default"  ; break;
            }

            if(chosen_plt_art.Child("FPS").ContainsAttribute(plt_size_name))
              fps = lexical_cast<double>(chosen_plt_art.Child("FPS").Attribute(plt_size_name));
          }

          if(chosen_plt_art.ContainsChild("StartFrame"))
          {
            if(chosen_plt_art.Child("StartFrame").ContainsAttribute("value"))
              start_frame=lexical_cast<int>(chosen_plt_art.Child("StartFrame").Attribute("value"));
          }
        }
      }
    }

    if(textures.size()==0)
      throw std::runtime_error("::GetPlanetTexturesDynamic: no dynamic textures found!");

    return textures.size();
  }

  int GetPlanetTextures(const Planet &planet,std::vector<boost::shared_ptr<GG::Texture> > &textures, int &start_frame, double &fps)
  {
#if 0 // this code causes a segfault in Linux, and the dynamic textures code is not used right now any more anyway
    try
    {
      return GetPlanetTexturesDynamic(GetPlanetArtNodeName(planet),planet.Size(),planet.ID(),textures,start_frame,fps);
    }
    catch(...)
    {
      textures.push_back(GetPlanetTextureStatic(planet));
      return textures.size();
    }
#else
      textures.push_back(GetPlanetTextureStatic(planet));
      return textures.size();
#endif
  }

  std::string GetPlanetSizeName(const Planet &planet)
  {
    if (planet.Size() == SZ_ASTEROIDS || planet.Size() == SZ_GASGIANT)
      return "";
    return UserString(lexical_cast<std::string>(planet.Size()));
  }

  std::string GetPlanetTypeName(const Planet &planet)
  {
    return UserString(lexical_cast<std::string>(planet.Type()));
  }

  Ship* FindColonyShip(int system_id)
  {
    const System *system = GetUniverse().Object<const System>(system_id);
    if(system==0)
      return 0;

    std::vector<const Fleet*> flt_vec = system->FindObjects<Fleet>();

    Ship* ship=0;

    for(unsigned int i=0;i<flt_vec.size();i++)
      if(flt_vec[i]->Owners().find(HumanClientApp::GetApp()->EmpireID()) != flt_vec[i]->Owners().end())
      {
        Ship* s=0;
        for(Fleet::const_iterator it = flt_vec[i]->begin(); it != flt_vec[i]->end(); ++it)
          if(   (s=GetUniverse().Object<Ship>(*it))
             && s->Design()->colonize)
          {
            ship = s;

            // prefere non moving colony ship
            if(!flt_vec[i]->Accept(StationaryFleetVisitor(*flt_vec[i]->Owners().begin())))
              break;
            return s;
          }
      }

    return ship;
  }

  void AngledCornerRectangle(int x1, int y1, int x2, int y2, GG::Clr color, GG::Clr border, int thick, 
                             int upper_left_angle_offset, int upper_right_angle_offset, 
                             int lower_right_angle_offset, int lower_left_angle_offset)
  {
    glDisable(GL_TEXTURE_2D);

    int inner_x1 = x1 + thick;
    int inner_y1 = y1 + thick;
    int inner_x2 = x2 - thick;
    int inner_y2 = y2 - thick;

    // these are listed in CCW order for convenience
    int ul_corner_x1 = x1 + upper_left_angle_offset ; int ul_corner_y1 = y1;
    int ul_corner_x2 = x1                           ; int ul_corner_y2 = y1 + upper_left_angle_offset;
    int lr_corner_x1 = x2 - lower_right_angle_offset; int lr_corner_y1 = y2;
    int lr_corner_x2 = x2                           ; int lr_corner_y2 = y2 - lower_right_angle_offset;

    int ll_corner_x1 = x1 + lower_left_angle_offset ; int ll_corner_y1 = y2;
    int ll_corner_x2 = x1                           ; int ll_corner_y2 = y2 - lower_left_angle_offset;
    int ur_corner_x1 = x2 - upper_right_angle_offset; int ur_corner_y1 = y1;
    int ur_corner_x2 = x2                           ; int ur_corner_y2 = y1 + upper_right_angle_offset;

    int inner_ul_corner_x1 = ul_corner_x1 + thick; int inner_ul_corner_y1 = ul_corner_y1 + thick;
    int inner_ul_corner_x2 = ul_corner_x2 + thick; int inner_ul_corner_y2 = ul_corner_y2 + thick;
    int inner_lr_corner_x1 = lr_corner_x1 - thick; int inner_lr_corner_y1 = lr_corner_y1 - thick;
    int inner_lr_corner_x2 = lr_corner_x2 - thick; int inner_lr_corner_y2 = lr_corner_y2 - thick;

    int inner_ll_corner_x1 = ll_corner_x1 + thick; int inner_ll_corner_y1 = ll_corner_y1 - thick;
    int inner_ll_corner_x2 = ll_corner_x2 + thick; int inner_ll_corner_y2 = ll_corner_y2 - thick;
    int inner_ur_corner_x1 = ur_corner_x1 - thick; int inner_ur_corner_y1 = ur_corner_y1 + thick;
    int inner_ur_corner_x2 = ur_corner_x2 - thick; int inner_ur_corner_y2 = ur_corner_y2 + thick;

    // draw beveled edges
    if (thick) 
    {
      glBegin(GL_QUADS);
      glColor4ubv(border.v);

      // the top =>
      if(upper_right_angle_offset>0)
      {
        glVertex2i(inner_ur_corner_x1, inner_ur_corner_y1);
        glVertex2i(ur_corner_x1, ur_corner_y1);
      }
      else
      {
        glVertex2i(inner_x2, inner_y1);
        glVertex2i(x2, y1);
      }

      if(upper_left_angle_offset>0)
      {
        glVertex2i(ul_corner_x1, ul_corner_y1);
        glVertex2i(inner_ul_corner_x1, inner_ul_corner_y1);
	  } 
      else
      {
        glVertex2i(x1, y1);
        glVertex2i(inner_x1, inner_y1);
      }
      // <= the top 

      // the left side => 
      if(upper_left_angle_offset>0)
      {
        glVertex2i(inner_ul_corner_x2, inner_ul_corner_y2);
        glVertex2i(ul_corner_x2, ul_corner_y2);
      } 
      else
      {
        glVertex2i(inner_x1, inner_y1);
        glVertex2i(x1, y1);
      }

      if(lower_left_angle_offset>0)
      {
        glVertex2i(ll_corner_x2,ll_corner_y2);
        glVertex2i(inner_ll_corner_x2,inner_ll_corner_y2);
      }
      else
      {
        glVertex2i(x1, y2);
        glVertex2i(inner_x1, inner_y2);
      }
      // <= the left side 

      // the bottom =>
      if(lower_left_angle_offset>0)
      {
        glVertex2i(inner_ll_corner_x1,inner_ll_corner_y1);
        glVertex2i(ll_corner_x1,ll_corner_y1);
      }
      else
      {
        glVertex2i(inner_x1, inner_y2);
        glVertex2i(x1, y2);
      }
      if(lower_right_angle_offset>0)
      {
        glVertex2i(lr_corner_x1, lr_corner_y1);
        glVertex2i(inner_lr_corner_x1, inner_lr_corner_y1);
      }
      else
      {
        glVertex2i(x2, y2);
        glVertex2i(inner_x2, inner_y2);
      }
      // <= the bottom 

      // the right side =>
      if(lower_right_angle_offset>0)
      {
        glVertex2i(inner_lr_corner_x2, inner_lr_corner_y2);
        glVertex2i(lr_corner_x2, lr_corner_y2);
      }
      else
      {
        glVertex2i(inner_x2, inner_y2);
        glVertex2i(x2, y2);
      }
      if(upper_right_angle_offset>0)
      {
        glVertex2i(ur_corner_x2, ur_corner_y2);
        glVertex2i(inner_ur_corner_x2, inner_ur_corner_y2);
      }
      else
      {
        glVertex2i(x2, y1);
        glVertex2i(inner_x2, inner_y1);
      }
      // <= the right side


      // the lower-right angled side
      if(lower_right_angle_offset>0) 
      {
        glVertex2i(inner_lr_corner_x1, inner_lr_corner_y1);
        glVertex2i(lr_corner_x1, lr_corner_y1);
        glVertex2i(lr_corner_x2, lr_corner_y2);
        glVertex2i(inner_lr_corner_x2, inner_lr_corner_y2);
      }

      // the upper-left angled side
      if(upper_left_angle_offset>0) 
      {
        glVertex2i(inner_ul_corner_x1, inner_ul_corner_y1);
        glVertex2i(ul_corner_x1, ul_corner_y1);
        glVertex2i(ul_corner_x2, ul_corner_y2);
        glVertex2i(inner_ul_corner_x2, inner_ul_corner_y2);
      }

      // the upper-right angled side
      if(upper_right_angle_offset>0) 
      {
        glVertex2i(inner_ur_corner_x1, inner_ur_corner_y1);
        glVertex2i(ur_corner_x1, ur_corner_y1);
        glVertex2i(ur_corner_x2, ur_corner_y2);
        glVertex2i(inner_ur_corner_x2, inner_ur_corner_y2);
      }

      // the lower-left angled side
      if(lower_left_angle_offset>0) 
      {
        glVertex2i(inner_ll_corner_x1, inner_ll_corner_y1);
        glVertex2i(ll_corner_x1, ll_corner_y1);
        glVertex2i(ll_corner_x2, ll_corner_y2);
        glVertex2i(inner_ll_corner_x2, inner_ll_corner_y2);
      }

      glEnd();
    }

    // draw interior of rectangle
    if(!(color==GG::CLR_ZERO))
    {
      glColor4ubv(color.v);
      glBegin(GL_POLYGON);
      if(upper_left_angle_offset>0)
      {
        glVertex2i(inner_ul_corner_x1, inner_ul_corner_y1);
        glVertex2i(inner_ul_corner_x2, inner_ul_corner_y2);
      } 
      else 
        glVertex2i(inner_x1, inner_y1);


      if(lower_left_angle_offset>0)
      {
        glVertex2i(inner_ll_corner_x2, inner_ll_corner_y2);
        glVertex2i(inner_ll_corner_x1, inner_ll_corner_y1);
      } 
      else 
        glVertex2i(inner_x1, inner_y2);

      if(lower_right_angle_offset>0)
      {
        glVertex2i(inner_lr_corner_x1, inner_lr_corner_y1);
        glVertex2i(inner_lr_corner_x2, inner_lr_corner_y2);
      }
      else
        glVertex2i(inner_x2, inner_y2);

      if(upper_right_angle_offset>0)
      {
        glVertex2i(inner_ur_corner_x2, inner_ur_corner_y2);
        glVertex2i(inner_ur_corner_x1, inner_ur_corner_y1);
      }
      else
        glVertex2i(inner_x2, inner_y1);

      glEnd();
    }

    glEnable(GL_TEXTURE_2D);
  }

  bool InAngledCornerRect(const GG::Pt& pt, int x1, int y1, int x2, int y2, 
                             int upper_left_angle_offset, int upper_right_angle_offset, 
                             int lower_right_angle_offset, int lower_left_angle_offset)
  {
    GG::Pt ul(x1, y1),lr(x2, y2);
    if(!(ul <= pt && pt < lr))
      return false;

    GG::Pt dist = pt-GG::Pt(x1,y1);
    if((upper_left_angle_offset>0) && (dist.x*dist.x + dist.y*dist.y < upper_left_angle_offset*upper_left_angle_offset))
      return false;

    dist = pt-GG::Pt(x2,y1);
    if((upper_right_angle_offset>0) && (dist.x*dist.x + dist.y*dist.y < upper_right_angle_offset*upper_right_angle_offset))
      return false;

    dist = pt-GG::Pt(x2,y2);
    if((lower_right_angle_offset>0) && (dist.x*dist.x + dist.y*dist.y < lower_right_angle_offset*lower_right_angle_offset))
      return false;

    dist = pt-GG::Pt(x1,y2);
    if((lower_left_angle_offset>0) && (dist.x*dist.x + dist.y*dist.y < lower_left_angle_offset*lower_left_angle_offset))
      return false;

    return true;
  }
}
class CUITextureButton : public GG::Button
{
  public:
    /** \name Structors */ //@{
    CUITextureButton(int x, int y, int w, int h,const boost::shared_ptr<GG::Texture> &texture,GG::Clr color = ClientUI::BUTTON_COLOR, GG::Clr border = ClientUI::CTRL_BORDER_COLOR, int thick = 1, Uint32 flags = GG::Wnd::CLICKABLE)
      :  Button(x, y, w, h, "", "", 0, color, GG::CLR_ZERO, flags),
         m_texture(GG::SubTexture(texture,0,0,texture->DefaultWidth(),texture->DefaultWidth())),
	 m_border_color(border), m_border_thick(thick)
    {}

    //@}

    /** \name Accessors */ //@{
    const GG::SubTexture& Texture() const {return m_texture;}
    //@}

    /** \name Mutators control */ //@{
    virtual void   SetBorderColor(GG::Clr c) {m_border_color=c;}   ///< sets the control's border color;   
    //@}

  protected:
    /** \name Mutators control */ //@{
    virtual void RenderPressed () {OffsetMove(1, 1);RenderUnpressed();OffsetMove(-1, -1);}
    virtual void RenderRollover() {RenderUnpressed();}
    virtual void RenderUnpressed()
    { 
      GG::Clr color_to_use = Disabled() ? DisabledColor(Color()) : Color();
      GG::Clr border_color_to_use = Disabled() ? DisabledColor(m_border_color) : m_border_color;

      GG::Pt ul = UpperLeft(), lr = LowerRight();
      AngledCornerRectangle(ul.x, ul.y, lr.x, lr.y,color_to_use,border_color_to_use,m_border_thick,0,0,0,0);    
      
      glColor4ubv(GG::CLR_WHITE.v);
      m_texture.OrthoBlit(ul,lr,false);
    }
    //@}

    virtual void   RButtonDown(const GG::Pt& pt, Uint32 keys)          {if (!Disabled()) SetState(BN_PRESSED);}
    virtual void   RDrag(const GG::Pt& pt, const GG::Pt& move, Uint32 keys){if (!Disabled()) SetState(BN_PRESSED);}
    virtual void   RButtonUp(const GG::Pt& pt, Uint32 keys)            {if (!Disabled()) SetState(BN_UNPRESSED);}
    virtual void   RClick(const GG::Pt& pt, Uint32 keys)               {if (!Disabled()) {SetState(BN_UNPRESSED); m_rclicked_sig(); SetState(BN_UNPRESSED);}}

  private:
    void Refresh();

    mutable ClickedSignalType m_rclicked_sig;

    GG::SubTexture m_texture;

    GG::Clr m_border_color;
    int     m_border_thick;
};

class CUIIconButton : public GG::Button
{
  public:
    enum Styles {IBS_LEFT,IBS_RIGHT,IBS_HCENTER,IBS_TOP,IBS_BOTTOM,IBS_VCENTER,IBS_OVERLAPPED};

    /** \name Structors */ //@{
    CUIIconButton(int x, int y, int w, int h,const boost::shared_ptr<GG::Texture> &icon, const std::string& font_filename = ClientUI::FONT, int pts = ClientUI::PTS, 
          GG::Clr color = ClientUI::BUTTON_COLOR, GG::Clr border = ClientUI::CTRL_BORDER_COLOR, int thick = 1, 
          GG::Clr text_color = ClientUI::TEXT_COLOR, Uint32 flags = GG::Wnd::CLICKABLE); ///< basic ctor
    //CUIIconButton(const GG::XMLElement& elem); ///< ctor that constructs a CUIScroll::ScrollTab object from an XMLElement. \throw std::invalid_argument May throw std::invalid_argument if \a elem does not encode a CUIScroll::ScrollTab object
    //@}

    /** \name Accessors */ //@{
    virtual bool            InWindow(const GG::Pt& pt) const;
    //virtual GG::XMLElement  XMLEncode() const; ///< constructs an XMLElement from a CUIScroll::ScrollTab object
    ClickedSignalType& RClickedSignal() const                    {return m_rclicked_sig;} ///< returns the clicked signal object for this Button

    const GG::SubTexture& Icon() const {return m_icon;}
    const GG::Rect& IconRect() const {return m_icon_rect;}
    const GG::Rect& TextRect() const {return m_text_rect;}

    //@}

    /** \name Mutators control */ //@{
    virtual void   SetBorderColor(GG::Clr c) {m_border_color=c;}   ///< sets the control's border color;
    
    void SetValue(double value); ///< sets the value to be displayed
    void SetDecimalsShown(int d)     {m_decimals_to_show = d; SetValue(m_value);} ///< sets the number of places after the decimal point to be shown
    void ShowSign(bool b)            {m_show_sign = b; SetValue(m_value);}        ///< sets whether a sign should always be shown, even for positive values
    void SetPositiveColor(GG::Clr c) {m_positive_color = c; SetValue(m_value);}   ///< sets the color that will be used to display positive values
    void SetNegativeColor(GG::Clr c) {m_negative_color = c; SetValue(m_value);}   ///< sets the color that will be used to display negative values
    
    void SetAngledCornerUpperLeft (int angled_corner) {m_angled_corner_upperleft =angled_corner;} 
    void SetAngledCornerUpperRight(int angled_corner) {m_angled_corner_upperright=angled_corner;} 
    void SetAngledCornerLowerRight(int angled_corner) {m_angled_corner_lowerright=angled_corner;} 
    void SetAngledCornerLowerLeft (int angled_corner) {m_angled_corner_lowerleft =angled_corner;} 

    void SetIconRect(const GG::Rect &rect) {m_icon_rect=rect;}
    void SetTextRect(const GG::Rect &rect) {m_text_rect=rect;}
    
    //@}

  protected:
    /** \name Mutators control */ //@{
    virtual void RenderPressed();
    virtual void RenderRollover();
    virtual void RenderUnpressed();
    //@}

    virtual void   RButtonDown(const GG::Pt& pt, Uint32 keys)          {if (!Disabled()) SetState(BN_PRESSED);}
    virtual void   RDrag(const GG::Pt& pt, const GG::Pt& move, Uint32 keys){if (!Disabled()) SetState(BN_PRESSED);}
    virtual void   RButtonUp(const GG::Pt& pt, Uint32 keys)            {if (!Disabled()) SetState(BN_UNPRESSED);}
    virtual void   RClick(const GG::Pt& pt, Uint32 keys)               {if (!Disabled()) {SetState(BN_UNPRESSED); m_rclicked_sig(); SetState(BN_UNPRESSED);}}

  private:
    void Refresh();

    mutable ClickedSignalType m_rclicked_sig;

    double m_value;
    int m_decimals_to_show;
    bool m_show_sign;
    GG::Clr m_positive_color;
    GG::Clr m_negative_color;

    GG::SubTexture m_icon;

    GG::Clr m_border_color;
    int     m_border_thick;

    int     m_angled_corner_upperleft,m_angled_corner_upperright,m_angled_corner_lowerright,m_angled_corner_lowerleft;

    GG::Rect  m_icon_rect,m_text_rect;
};

CUIIconButton::CUIIconButton( int x, int y, int w, int h,const boost::shared_ptr<GG::Texture> &icon,const std::string& font_filename, 
                              int pts, GG::Clr color, GG::Clr border, int thick, GG::Clr text_color, Uint32 flags)
:  Button(x, y, w, h, "", font_filename, pts, color, text_color, flags),
   m_value(0.0),m_decimals_to_show(0),m_show_sign(true),m_positive_color(text_color),m_negative_color(text_color),
   m_icon(GG::SubTexture(icon,0,0,icon->DefaultWidth(),icon->DefaultWidth())),
   m_border_color(border), m_border_thick(thick),
   m_angled_corner_upperleft(0),m_angled_corner_upperright(0),m_angled_corner_lowerright(0),m_angled_corner_lowerleft(0),
   m_icon_rect(1+m_border_thick,1+m_border_thick,h-(1+m_border_thick),h-(1+m_border_thick)),m_text_rect(m_icon_rect.LowerRight().x,2,w-2,h-2)
{
  SetTextFormat(GG::TF_RIGHT | GG::TF_VCENTER);
  SetValue(0.0);
}

bool CUIIconButton::InWindow(const GG::Pt& pt) const
{
  GG::Pt ul = UpperLeft(), lr = LowerRight();
  return InAngledCornerRect(pt,ul.x, ul.y, lr.x, lr.y,m_angled_corner_upperleft,m_angled_corner_upperright,m_angled_corner_lowerright,m_angled_corner_lowerleft);    
}

void CUIIconButton::SetValue(double value) 
{
  m_value = value;
  if(m_decimals_to_show)
  { 
    char buf[128];
    sprintf(buf, (m_show_sign ? "%+#.*g" : "%#.*g"), m_decimals_to_show, value);
    SetText(buf);
  } 
  else
    SetText((m_show_sign?(m_value < 0.0?"-":"+"):"") + lexical_cast<std::string>(static_cast<int>(value)));
  
  SetTextColor(m_value < 0.0 ? m_negative_color : m_positive_color);
}

void CUIIconButton::RenderPressed()
{
  OffsetMove(1, 1);
  RenderUnpressed();
  OffsetMove(-1, -1);
}

void CUIIconButton::RenderRollover()
{
  RenderUnpressed();
}

void CUIIconButton::RenderUnpressed()
{ 
  GG::Clr color_to_use = Disabled() ? DisabledColor(Color()) : Color();
  GG::Clr border_color_to_use = Disabled() ? DisabledColor(m_border_color) : m_border_color;

  GG::Pt ul = UpperLeft(), lr = LowerRight();
  AngledCornerRectangle(ul.x, ul.y, lr.x, lr.y,color_to_use,border_color_to_use,m_border_thick,
                        m_angled_corner_upperleft,m_angled_corner_upperright,m_angled_corner_lowerright,m_angled_corner_lowerleft);    
  //glColor4ubv(Disabled() ? DisabledColor(m_color).v : m_color.v);
  
  glColor4ubv(GG::CLR_WHITE.v);
  m_icon.OrthoBlit(UpperLeft()+m_icon_rect.UpperLeft(),UpperLeft()+m_icon_rect.LowerRight(),false);

  std::string text; GG::SubTexture icon;

  Uint32 format = TextFormat();
  
  glColor4ubv(TextColor().v);
  GetFont()->RenderText(UpperLeft()+m_text_rect.UpperLeft(),UpperLeft()+m_text_rect.LowerRight(), *this, format, 0);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
SidePanel::PlanetPanel::PlanetPanel(int x, int y, int w, int h, const Planet &planet, StarType star_type)
: Wnd(0, y, w, h, GG::Wnd::CLICKABLE),
  m_planet_id(planet.ID()),
  m_planet_name(0),m_planet_info(0),
  m_button_colonize(0),
  m_planet_graphic(0),
  m_rotating_planet_graphic(0),
  m_button_food(0),m_button_mining(0),m_button_industry(0),m_button_research(0),m_button_balanced(0)
{
  SetText(UserString("PLANET_PANEL"));

  m_planet_name = new GG::TextControl(MAX_PLANET_DIAMETER-15,10,planet.Name(),ClientUI::FONT,ClientUI::SIDE_PANEL_PLANET_NAME_PTS,ClientUI::TEXT_COLOR);
  AttachChild(m_planet_name);

  GG::Pt ul = UpperLeft(), lr = LowerRight();
  int planet_image_sz = PlanetDiameter();
  GG::Pt planet_image_pos(MAX_PLANET_DIAMETER / 2 - planet_image_sz / 2, Height() / 2 - planet_image_sz / 2);

#if ROTATING_PLANET_IMAGES
  if (0)//TODO: revert to planet.Type() == PT_ASTEROIDS)
  {
#endif
      std::vector<boost::shared_ptr<GG::Texture> > textures; int start_frame; double fps;

      //#texures holds at least one element or GetPlanetTextures throws an exception
      GetPlanetTextures(planet,textures,start_frame=-1,fps=0.0);
      m_planet_graphic = new GG::DynamicGraphic(planet_image_pos.x,planet_image_pos.y,planet_image_sz,planet_image_sz,true,textures[0]->DefaultWidth(),textures[0]->DefaultHeight(),0,textures, GG::GR_FITGRAPHIC | GG::GR_PROPSCALE);
      if(start_frame==-1 && 1<textures.size())
          start_frame = RandSmallInt(0,textures.size()-1);

      if(start_frame!=-1 && fps!=0.0)
          m_planet_graphic->SetTimeIndex(static_cast<int>(start_frame * 1000.0 / m_planet_graphic->FPS()));
      AttachChild(m_planet_graphic);m_planet_graphic->Play();

      textures.clear();
#if ROTATING_PLANET_IMAGES
  }
  else if (planet.Type() < NUM_PLANET_TYPES)
  {
      const std::map<PlanetType, std::vector<RotatingPlanetData> >& planet_data = GetRotatingPlanetData();
      std::map<PlanetType, std::vector<RotatingPlanetData> >::const_iterator it = planet_data.find(planet.Type());
      int num_planets_of_type;
      if (it != planet_data.end() && (num_planets_of_type = planet_data.find(planet.Type())->second.size()))
      {
          m_rotating_planet_graphic = new RotatingPlanetControl(planet_image_pos.x, planet_image_pos.y, planet.Size(), star_type,
                                                                it->second[planet.ID() % num_planets_of_type]);
          AttachChild(m_rotating_planet_graphic);
      }
  }
#endif

  m_planet_info = new GG::TextControl(m_planet_name->UpperLeft().x-UpperLeft().x+10,m_planet_name->LowerRight().y-UpperLeft().y,"",ClientUI::FONT,ClientUI::SIDE_PANEL_PTS,ClientUI::TEXT_COLOR,GG::TF_LEFT|GG::TF_TOP);
  AttachChild(m_planet_info);

  m_button_colonize = new CUIButton((Width()/3)*2,(Height()-ClientUI::SIDE_PANEL_PTS)/2,60,UserString("PL_COLONIZE"),ClientUI::FONT,ClientUI::SIDE_PANEL_PTS,ClientUI::BUTTON_COLOR,ClientUI::CTRL_BORDER_COLOR,1,ClientUI::TEXT_COLOR,GG::Wnd::CLICKABLE);
  Connect(m_button_colonize->ClickedSignal(), &SidePanel::PlanetPanel::ClickColonize, this);
  AttachChild(m_button_colonize);

  const int RESOURCE_DISPLAY_HEIGHT = 2*ClientUI::PTS;
  const int RESOURCE_DISPLAY_WIDTH  = 60;
  const int RESOURCE_DISPLAY_MARGIN = 8;

  ul=GG::Pt(Width()-2*RESOURCE_DISPLAY_WIDTH-2*RESOURCE_DISPLAY_MARGIN,m_planet_name->LowerRight().y-UpperLeft().y/*-(m_planet_name->LowerRight().y-m_planet_name->UpperLeft().y)/2*/);
  m_button_food     = new CUIIconButton(ul.x                                               ,ul.y                                                ,RESOURCE_DISPLAY_WIDTH,RESOURCE_DISPLAY_HEIGHT,IconFarming (),ClientUI::FONT,10,GG::CLR_ZERO,ClientUI::CTRL_BORDER_COLOR,2);
  m_button_mining   = new CUIIconButton(ul.x+RESOURCE_DISPLAY_WIDTH+RESOURCE_DISPLAY_MARGIN,ul.y                                                ,RESOURCE_DISPLAY_WIDTH,RESOURCE_DISPLAY_HEIGHT,IconMining  (),ClientUI::FONT,10,GG::CLR_ZERO,ClientUI::CTRL_BORDER_COLOR,2);
  m_button_research = new CUIIconButton(ul.x                                               ,ul.y+RESOURCE_DISPLAY_HEIGHT+RESOURCE_DISPLAY_MARGIN,RESOURCE_DISPLAY_WIDTH,RESOURCE_DISPLAY_HEIGHT,IconResearch(),ClientUI::FONT,10,GG::CLR_ZERO,ClientUI::CTRL_BORDER_COLOR,2);
  m_button_industry = new CUIIconButton(ul.x+RESOURCE_DISPLAY_WIDTH+RESOURCE_DISPLAY_MARGIN,ul.y+RESOURCE_DISPLAY_HEIGHT+RESOURCE_DISPLAY_MARGIN,RESOURCE_DISPLAY_WIDTH,RESOURCE_DISPLAY_HEIGHT,IconIndustry(),ClientUI::FONT,10,GG::CLR_ZERO,ClientUI::CTRL_BORDER_COLOR,2);

  const boost::shared_ptr<GG::Texture> icon=IconBalance ();
  m_button_balanced = new CUIIconButton(m_button_food->LowerRight().x-7,m_button_food->LowerRight().y-7,
                                        m_button_industry->UpperLeft().x-m_button_food->LowerRight().x+2*7,
                                        m_button_industry->UpperLeft().y-m_button_food->LowerRight().y+2*7,
                                        icon,ClientUI::FONT,ClientUI::PTS ,GG::CLR_ZERO,GG::CLR_ZERO               ,1);

  //m_button_balanced->SetIconRect(GG::Rect(3,3,m_button_balanced->Width()-4,m_button_balanced->Height()-4));
  m_button_balanced->SetIconRect(GG::Rect((m_button_balanced->Width ()-icon->DefaultWidth ())/2,
                                          (m_button_balanced->Height()-icon->DefaultHeight())/2,
                                          (m_button_balanced->Width ()-icon->DefaultWidth ())/2+icon->DefaultWidth (),
                                          (m_button_balanced->Height()-icon->DefaultHeight())/2+icon->DefaultHeight()));

  m_button_food    ->SetTextRect(GG::Rect(m_button_food    ->IconRect().UpperLeft().x+2,2,m_button_food    ->Width()-6,m_button_food    ->Height()-2));
  m_button_mining  ->SetTextRect(GG::Rect(m_button_mining  ->IconRect().UpperLeft().x+2,2,m_button_mining  ->Width()-6,m_button_mining  ->Height()-2));
  m_button_research->SetTextRect(GG::Rect(m_button_research->IconRect().UpperLeft().x+2,2,m_button_research->Width()-6,m_button_research->Height()-2));
  m_button_industry->SetTextRect(GG::Rect(m_button_industry->IconRect().UpperLeft().x+2,2,m_button_industry->Width()-6,m_button_industry->Height()-2));

  m_button_food     ->SetAngledCornerLowerRight(7);
  m_button_mining   ->SetAngledCornerLowerLeft (7);
  m_button_research ->SetAngledCornerUpperRight(7);
  m_button_industry ->SetAngledCornerUpperLeft (7);


  m_button_balanced ->SetAngledCornerLowerRight(7);
  m_button_balanced ->SetAngledCornerLowerLeft (7);
  m_button_balanced ->SetAngledCornerUpperRight(7);
  m_button_balanced ->SetAngledCornerUpperLeft (7);

  m_button_food    ->SetPositiveColor(GG::CLR_GREEN);m_button_food    ->SetNegativeColor(GG::CLR_RED );
  //m_button_mining  ->SetPositiveColor(GG::CLR_ZERO );m_button_mining  ->SetNegativeColor(GG::CLR_ZERO);
  //m_button_research->SetPositiveColor(GG::CLR_ZERO );m_button_research->SetNegativeColor(GG::CLR_ZERO);
  //m_button_industry->SetPositiveColor(GG::CLR_ZERO );m_button_industry->SetNegativeColor(GG::CLR_ZERO);
  m_button_balanced->SetPositiveColor(GG::CLR_ZERO );m_button_balanced->SetNegativeColor(GG::CLR_ZERO);

  Connect(m_button_food    ->ClickedSignal (), &SidePanel::PlanetPanel::LClickFarming , this);
  Connect(m_button_food    ->RClickedSignal(), &SidePanel::PlanetPanel::RClickFarming , this);
  Connect(m_button_mining  ->ClickedSignal (), &SidePanel::PlanetPanel::LClickMining  , this);
  Connect(m_button_mining  ->RClickedSignal(), &SidePanel::PlanetPanel::RClickMining  , this);
  Connect(m_button_research->ClickedSignal (), &SidePanel::PlanetPanel::LClickResearch, this);
  Connect(m_button_research->RClickedSignal(), &SidePanel::PlanetPanel::RClickResearch, this);
  Connect(m_button_industry->ClickedSignal (), &SidePanel::PlanetPanel::LClickIndustry, this);
  Connect(m_button_industry->RClickedSignal(), &SidePanel::PlanetPanel::RClickIndustry, this);
  Connect(m_button_balanced->ClickedSignal (), &SidePanel::PlanetPanel::LClickBalanced, this);
  Connect(m_button_balanced->RClickedSignal(), &SidePanel::PlanetPanel::RClickBalanced, this);

  // UI sounds
  GG::Connect(m_button_food    ->ClickedSignal (), &PlayFarmingFocusClickSound);
  GG::Connect(m_button_food    ->RClickedSignal(), &PlayFarmingFocusClickSound);
  GG::Connect(m_button_mining  ->ClickedSignal (), &PlayMiningFocusClickSound);
  GG::Connect(m_button_mining  ->RClickedSignal(), &PlayMiningFocusClickSound);
  GG::Connect(m_button_research->ClickedSignal (), &PlayResearchFocusClickSound);
  GG::Connect(m_button_research->RClickedSignal(), &PlayResearchFocusClickSound);
  GG::Connect(m_button_industry->ClickedSignal (), &PlayIndustryFocusClickSound);
  GG::Connect(m_button_industry->RClickedSignal(), &PlayIndustryFocusClickSound);
  GG::Connect(m_button_balanced->ClickedSignal (), &PlayBalancedFocusClickSound);
  GG::Connect(m_button_balanced->RClickedSignal(), &PlayBalancedFocusClickSound);
  // TODO: connect trade when it gets added

  AttachChild(m_button_food);AttachChild(m_button_mining);AttachChild(m_button_industry);
  AttachChild(m_button_research);AttachChild(m_button_balanced);

  if (planet.Type() == PT_ASTEROIDS) 
  {
    MoveChildDown(m_planet_graphic);
  }

  const Planet *plt = GetUniverse().Object<const Planet>(m_planet_id);

  if (System* system = plt->GetSystem())
    m_connection_system_changed = GG::Connect(system->StateChangedSignal(), &SidePanel::PlanetPanel::PlanetChanged, this);
  m_connection_planet_changed = GG::Connect(plt->StateChangedSignal(), &SidePanel::PlanetPanel::PlanetChanged, this);
  m_connection_planet_production_changed= GG::Connect(plt->ResourceCenterChangedSignal(), &SidePanel::PlanetPanel::PlanetResourceCenterChanged, this);

  Update();
}

SidePanel::PlanetPanel::~PlanetPanel()
{
  for(unsigned int i=0;i<m_vec_unused_controls.size();i++)
    delete m_vec_unused_controls[i];
  m_vec_unused_controls.clear();
}

Planet* SidePanel::PlanetPanel::GetPlanet()
{
  Planet *planet = GetUniverse().Object<Planet>(m_planet_id);
  if(!planet)
    throw std::runtime_error("SidePanel::PlanetPanel::GetPlanet: planet not found!");
  return planet;
}

const Planet* SidePanel::PlanetPanel::GetPlanet() const
{
  const Planet *planet = GetUniverse().Object<const Planet>(m_planet_id);
  if(!planet)
    throw std::runtime_error("SidePanel::PlanetPanel::GetPlanet: planet not found!");
  return planet;
}

void SidePanel::PlanetPanel::Update()
{
  PlanetChanged();PlanetResourceCenterChanged();
}

void SidePanel::PlanetPanel::EnableControl(GG::Wnd *control,bool enable)
{
  std::vector<GG::Wnd*>::iterator it = std::find(m_vec_unused_controls.begin(),m_vec_unused_controls.end(),control);
  if(it != m_vec_unused_controls.end())
  {
    if(enable)
    {
      m_vec_unused_controls.erase(it);
      AttachChild(control);
      control->Show();
    }
  }
  else
  {
    if(!enable)
    {
      m_vec_unused_controls.push_back(control);
      DetachChild(control);
      control->Hide();
    }
  }
}

void SidePanel::PlanetPanel::PlanetChanged()
{
  const Planet *planet = GetPlanet();

  enum OWNERSHIP {OS_NONE,OS_FOREIGN,OS_SELF} owner = OS_NONE;

  std::string text;
  if(planet->Owners().size()==0 || planet->IsAboutToBeColonized()) 
  { //uninhabited
    owner = OS_NONE;
    text = GetPlanetSizeName(*planet);
    if(text.length()>0)
      text+=" ";
    text+= GetPlanetTypeName(*planet);
  
    text+="\n";
    if(planet->MaxPop()==0) text+= UserString("PE_UNINHABITABLE");
    else                    text+= UserString("PL_SIZE") + " " + lexical_cast<std::string>(planet->MaxPop());

    m_planet_info->SetText(text);
  }
  else 
    if(!planet->OwnedBy(HumanClientApp::GetApp()->EmpireID()))
    {//inhabited
      owner = OS_FOREIGN;
      text = GetPlanetSizeName(*planet);
      if(text.length()>0)
        text+=" ";
      text+= GetPlanetTypeName(*planet);
    
      m_planet_info->SetText(text);
    }
    else
    {//Owned
      owner = OS_SELF;
      text = GetPlanetSizeName(*planet);
      if(text.length()>0)
        text+="\n";
      text+= GetPlanetTypeName(*planet);
      
      m_planet_info->SetText(text);
    }

  if (!planet->Owners().empty()) {
      Empire* planet_empire = HumanClientApp::Empires().Lookup(*(planet->Owners().begin()));
      m_planet_name->SetTextColor(planet_empire?planet_empire->Color():ClientUI::TEXT_COLOR);
  }

  // visibility
  if (owner==OS_NONE 
      && planet->MaxPop()>0 
      && !planet->IsAboutToBeColonized()
      && FindColonyShip(planet->SystemID()))
  {
    std::vector<GG::Wnd*>::iterator it = std::find(m_vec_unused_controls.begin(),m_vec_unused_controls.end(),m_button_colonize);
    if(it != m_vec_unused_controls.end())
    {
      m_vec_unused_controls.erase(it);
      AttachChild(m_button_colonize);
      m_button_colonize->Show();
    }
    m_button_colonize->SetText(UserString("PL_COLONIZE"));
  }
  else if (planet->IsAboutToBeColonized())
  {
    std::vector<GG::Wnd*>::iterator it = std::find(m_vec_unused_controls.begin(),m_vec_unused_controls.end(),m_button_colonize);
    if(it != m_vec_unused_controls.end())
    {
      m_vec_unused_controls.erase(it);
      AttachChild(m_button_colonize);
      m_button_colonize->Show();
    }
    m_button_colonize->SetText(UserString("CANCEL"));
  }
  else
  {
     std::vector<GG::Wnd*>::iterator it = std::find(m_vec_unused_controls.begin(),m_vec_unused_controls.end(),m_button_colonize);
     if(it == m_vec_unused_controls.end())
     {
        m_vec_unused_controls.push_back(m_button_colonize);
        DetachChild(m_button_colonize);
        m_button_colonize->Hide();
     }
  }

  EnableControl(m_button_food    ,(owner==OS_SELF));
  EnableControl(m_button_mining  ,(owner==OS_SELF));
  EnableControl(m_button_industry,(owner==OS_SELF));
  EnableControl(m_button_research,(owner==OS_SELF));
  EnableControl(m_button_balanced,(owner==OS_SELF));
}

void SidePanel::PlanetPanel::PlanetResourceCenterChanged()
{
  TempUISoundDisabler sound_disabler;

  const Planet *planet = GetPlanet();

  m_button_food    ->SetValue(planet->FarmingPoints ());
  m_button_industry->SetValue(planet->IndustryPoints());
  m_button_mining  ->SetValue(planet->MiningPoints  ());
  m_button_research->SetValue(planet->ResearchPoints());

  m_button_food    ->SetColor((planet->PrimaryFocus()==FOCUS_BALANCED || planet->PrimaryFocus()==FOCUS_FARMING || planet->SecondaryFocus()==FOCUS_BALANCED || planet->SecondaryFocus()==FOCUS_FARMING )?GG::Clr(100,100,  0,200):GG::CLR_ZERO);
  m_button_mining  ->SetColor((planet->PrimaryFocus()==FOCUS_BALANCED || planet->PrimaryFocus()==FOCUS_MINING  || planet->SecondaryFocus()==FOCUS_BALANCED || planet->SecondaryFocus()==FOCUS_MINING  )?GG::Clr(100,  0,  0,200):GG::CLR_ZERO);
  m_button_industry->SetColor((planet->PrimaryFocus()==FOCUS_BALANCED || planet->PrimaryFocus()==FOCUS_INDUSTRY|| planet->SecondaryFocus()==FOCUS_BALANCED || planet->SecondaryFocus()==FOCUS_INDUSTRY)?GG::Clr(  0,  0,100,200):GG::CLR_ZERO);
  m_button_research->SetColor((planet->PrimaryFocus()==FOCUS_BALANCED || planet->PrimaryFocus()==FOCUS_RESEARCH|| planet->SecondaryFocus()==FOCUS_BALANCED || planet->SecondaryFocus()==FOCUS_RESEARCH)?GG::Clr(  0,100,  0,200):GG::CLR_ZERO);
  
  GG::Clr color;

  color = ClientUI::CTRL_BORDER_COLOR;
  if(planet->PrimaryFocus()==FOCUS_BALANCED || planet->PrimaryFocus()==FOCUS_FARMING)
    color = (planet->SecondaryFocus()==FOCUS_BALANCED || planet->SecondaryFocus()==FOCUS_FARMING)?GG::CLR_WHITE:GG::Clr(255,255,0,255);
  m_button_food->SetBorderColor(color);

  color = ClientUI::CTRL_BORDER_COLOR;
  if(planet->PrimaryFocus()==FOCUS_BALANCED || planet->PrimaryFocus()==FOCUS_MINING)
    color = (planet->SecondaryFocus()==FOCUS_BALANCED || planet->SecondaryFocus()==FOCUS_MINING) ?GG::CLR_WHITE:GG::Clr(255,0,0,255);
  m_button_mining->SetBorderColor(color);

  color = ClientUI::CTRL_BORDER_COLOR;
  if(planet->PrimaryFocus()==FOCUS_BALANCED || planet->PrimaryFocus()==FOCUS_INDUSTRY)
    color = (planet->SecondaryFocus()==FOCUS_BALANCED || planet->SecondaryFocus()==FOCUS_INDUSTRY)?GG::CLR_WHITE:GG::Clr(0,0,255,255);
  m_button_industry->SetBorderColor(color);

  color = ClientUI::CTRL_BORDER_COLOR;
  if(planet->PrimaryFocus()==FOCUS_BALANCED || planet->PrimaryFocus()==FOCUS_RESEARCH)
    color = (planet->SecondaryFocus()==FOCUS_BALANCED || planet->SecondaryFocus()==FOCUS_RESEARCH) ?GG::CLR_WHITE:GG::Clr(0,255,0,255);
  m_button_research->SetBorderColor(color);

}

void SidePanel::PlanetPanel::SetPrimaryFocus(FocusType focus)
{
  Planet *planet = GetPlanet();
  HumanClientApp::Orders().IssueOrder(new ChangeFocusOrder(HumanClientApp::GetApp()->EmpireID(),planet->ID(),focus,true));
}

void SidePanel::PlanetPanel::SetSecondaryFocus(FocusType focus)
{
  Planet *planet = GetPlanet();
  HumanClientApp::Orders().IssueOrder(new ChangeFocusOrder(HumanClientApp::GetApp()->EmpireID(),planet->ID(),focus,false));
} 

void SidePanel::PlanetPanel::MouseWheel(const GG::Pt& pt, int move, Uint32 keys)
{
  GG::Wnd *parent;
  if((parent=Parent()))
    parent->MouseWheel(pt,move,keys);
}

void SidePanel::PlanetPanel::MouseEnter(const GG::Pt& pt, Uint32 keys)
{
}

void SidePanel::PlanetPanel::MouseLeave(const GG::Pt& pt, Uint32 keys)
{
}

bool SidePanel::PlanetPanel::InWindow(const GG::Pt& pt) const
{
    GG::Pt ul = UpperLeft(), lr = LowerRight();
    ul.x += MAX_PLANET_DIAMETER / 2;
    return ((ul <= pt && pt < lr) || InPlanet(pt));
}

void SidePanel::PlanetPanel::LClick(const GG::Pt& pt, Uint32 keys) 
{
  if(InPlanet(pt))
  {
    if(GetOptionsDB().Get<bool>("UI.sound.enabled"))
      HumanClientApp::GetApp()->PlaySound(ClientUI::SoundDir() + GetOptionsDB().Get<std::string>("UI.sound.planet-button-click"));
	m_planet_image_lclick_sig(m_planet_id);
  }
}

bool SidePanel::PlanetPanel::RenderUnhabited(const Planet &planet)
{
  return true;
}

bool SidePanel::PlanetPanel::RenderInhabited(const Planet &planet)
{
  //if (planet.Type() != PT_ASTEROIDS)
    //RenderPlanet(UpperLeft() + GG::Pt(MAX_PLANET_DIAMETER / 2, Height() / 2), PlanetDiameter(), GG::App::GetApp()->GetTexture("moon.png"));

  glColor4ubv(ClientUI::TEXT_COLOR.v);
  boost::shared_ptr<GG::Font> font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT,ClientUI::SIDE_PANEL_PTS);
  Uint32 format = GG::TF_LEFT | GG::TF_BOTTOM;

  std::string text; int x,y;

  x = m_planet_name->UpperLeft ().x+10;
  y = m_planet_name->LowerRight().y+ 5;

  //text = GetPlanetSizeName(planet);
  //font->RenderText(x,y,x + 500, y+font->Height(), text, format, 0);
  y+=font->Height();

  //text = GetPlanetTypeName(planet);
  //font->RenderText(x,y,x + 500, y+font->Height(), text, format, 0);
  y+=font->Height();

  int population=static_cast<int>(planet.PopPoints());

  boost::shared_ptr<GG::Texture> icon;
  const int ICON_MARGIN    =  5;
  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*1.2));

  //population
  //x = m_planet_name->UpperLeft ().x+10; y = m_planet_name->LowerRight().y + RESOURCE_DISPLAY_HEIGHT+3;
  glColor4ubv(ClientUI::TEXT_COLOR.v);
  icon=IconPopulation(); icon->OrthoBlit(x,y,x+font->Height(),y+font->Height(), 0, false);
  x+=font->Height();
  text = lexical_cast<std::string>(population)+"/"+lexical_cast<std::string>(planet.MaxPop());
  font->RenderText(x,y,x + 500, y+font->Height(), text, format, 0);
  x+=font->TextExtent(text, format).x+ICON_MARGIN;

  return true;
}

bool SidePanel::PlanetPanel::RenderOwned(const Planet &planet)
{
  //if (planet.Type() != PT_ASTEROIDS)
    //RenderPlanet(UpperLeft() + GG::Pt(MAX_PLANET_DIAMETER / 2, Height() / 2), PlanetDiameter(), GG::App::GetApp()->GetTexture("moon.png"));

  glColor4ubv(ClientUI::TEXT_COLOR.v);
  boost::shared_ptr<GG::Font> font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT,ClientUI::SIDE_PANEL_PTS);
  Uint32 format = GG::TF_LEFT | GG::TF_BOTTOM;

  std::string text; int x,y;

  x = m_planet_name->UpperLeft ().x+10;
  y = m_planet_name->LowerRight().y+ 5;

  //text = GetPlanetSizeName(planet);
  //font->RenderText(x,y,x + 500, y+font->Height(), text, format, 0);
  y+=font->Height();

  //text = GetPlanetTypeName(planet);
  //font->RenderText(x,y,x + 500, y+font->Height(), text, format, 0);
  y+=font->Height();


  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*0.9));
  text = UserString("PL_PRIMARY_FOCUS") + " ";
  text += UserString(lexical_cast<std::string>(planet.PrimaryFocus()));
  font->RenderText(m_button_food->UpperLeft().x,
                   m_button_food->UpperLeft().y-font->Height(),
                   m_button_food->UpperLeft().x+ 500,
                   m_button_food->UpperLeft().y, text, format, 0);

  text = UserString("PL_SECONDARY_FOCUS") + " ";
  text += UserString(lexical_cast<std::string>(planet.SecondaryFocus()));
  font->RenderText(m_button_research->UpperLeft ().x,
                   m_button_research->LowerRight().y,
                   m_button_research->UpperLeft ().x+ 500,
                   m_button_research->LowerRight().y+font->Height(), text, format, 0);

  int farming=0,mining=0,research=0,industry=0;

  farming   +=static_cast<int>(planet.FarmingPoints());
  industry  +=static_cast<int>(planet.IndustryPoints());
  mining    +=static_cast<int>(planet.MiningPoints());
  research  +=static_cast<int>(planet.ResearchPoints());
  //defense   +=;


  boost::shared_ptr<GG::Texture> icon;
  const int ICON_MARGIN    =  5;
  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*1.2));

  //population
  //x = m_planet_name->UpperLeft ().x+10; y = m_planet_name->LowerRight().y + RESOURCE_DISPLAY_HEIGHT+3;
  glColor4ubv(ClientUI::TEXT_COLOR.v);
  icon=IconPopulation(); icon->OrthoBlit(x,y,x+font->Height(),y+font->Height(), 0, false);
  x+=font->Height();

  double future_pop_growth = static_cast<int>(planet.FuturePopGrowth()*100.0) / 100.0;
  if     (future_pop_growth<0.0)  text=GG::RgbaTag(GG::CLR_RED);
  else if(future_pop_growth>0.0)  text=GG::RgbaTag(GG::CLR_GREEN);
       else                       text=GG::RgbaTag(ClientUI::TEXT_COLOR);

  text+= lexical_cast<std::string>(static_cast<int>(planet.PopPoints())) + "</rgba>/"+lexical_cast<std::string>(planet.MaxPop());
  font->RenderText(x,y,x + 500, y+font->Height(), text, format, 0);
  x+=font->TextExtent(text, format).x+ICON_MARGIN;

  return true;
}

bool SidePanel::PlanetPanel::Render()
{
  const Planet *planet = GetPlanet();

  if(planet->Owners().size()==0 || planet->IsAboutToBeColonized())  
    RenderUnhabited(*planet);
  else 
    if(!planet->OwnedBy(HumanClientApp::GetApp()->EmpireID()))     
      RenderInhabited(*planet);
    else
      RenderOwned    (*planet);
  return true;
}

int SidePanel::PlanetPanel::PlanetDiameter() const
{
    return ::PlanetDiameter(GetPlanet()->Size());
}

bool SidePanel::PlanetPanel::InPlanet(const GG::Pt& pt) const
{
    GG::Pt center = UpperLeft() + GG::Pt(MAX_PLANET_DIAMETER / 2, Height() / 2);
    GG::Pt diff = pt - center;
    int r_squared = PlanetDiameter() * PlanetDiameter() / 4;
    return diff.x * diff.x + diff.y * diff.y <= r_squared;
}

void SidePanel::PlanetPanel::ClickColonize()
{
  const Planet *planet = GetPlanet();
  int empire_id = HumanClientApp::GetApp()->EmpireID();
  std::map<int, int> pending_colonization_orders = HumanClientApp::GetApp()->PendingColonizationOrders();
  std::map<int, int>::const_iterator it = pending_colonization_orders.find(planet->ID());
  if(it == pending_colonization_orders.end()) // colonize
  {
    Ship *ship=FindColonyShip(planet->SystemID());
    if(ship==0)
      throw std::runtime_error("SidePanel::PlanetPanel::ClickColonize ship not found!");

    if(!ship->GetFleet()->Accept(StationaryFleetVisitor(*ship->GetFleet()->Owners().begin())))
    {
      GG::ThreeButtonDlg dlg(320,200,UserString("SP_USE_DEPARTING_COLONY_SHIPS_QUESTION"),
                             ClientUI::FONT,ClientUI::PTS,ClientUI::WND_COLOR,ClientUI::CTRL_BORDER_COLOR,ClientUI::CTRL_COLOR,ClientUI::TEXT_COLOR,2,
                             "Yes","No");
      dlg.Run();

      if(dlg.Result()!=0)
        return;
    }

    HumanClientApp::Orders().IssueOrder(new FleetColonizeOrder( empire_id, ship->ID(), planet->ID()));
  }
  else // cancel colonization
  {
    const FleetColonizeOrder *col_order = dynamic_cast<const FleetColonizeOrder*>(HumanClientApp::Orders().ExamineOrder(it->second));
    int ship_id = col_order?col_order->ShipID():UniverseObject::INVALID_OBJECT_ID;

    HumanClientApp::Orders().RecindOrder(it->second);
    
    // if the ship now buils a fleet of its own, make sure that fleet appears
    // at a possibly opend FleetWnd
    Ship  *ship = GetUniverse().Object<Ship>(ship_id);
    Fleet *fleet= ship?GetUniverse().Object<Fleet>(ship->FleetID()):NULL;
    if(fleet)
      for( FleetWnd::FleetWndItr it = FleetWnd::FleetWndBegin();it != FleetWnd::FleetWndEnd();++it)
      {
        FleetWnd *fleet_wnd = *it;
        if(   fleet->SystemID() == fleet_wnd->SystemID()
          && !fleet_wnd->ContainsFleet(fleet->ID()))
        {
          fleet_wnd->AddFleet(GetUniverse().Object<Fleet>(fleet->ID()));
          break;
        }
      }
  }
}

void SidePanel::PlanetPanel::RClick(const GG::Pt& pt, Uint32 keys)
{
  const Planet *planet = GetPlanet();
  
  if(!planet->OwnedBy(HumanClientApp::GetApp()->EmpireID()))
    return;


  GG::MenuItem menu_contents;
  menu_contents.next_level.push_back(GG::MenuItem(UserString("SP_RENAME_PLANET"), 1, false, false));
  GG::PopupMenu popup(pt.x, pt.y, GG::App::GetApp()->GetFont(ClientUI::FONT, ClientUI::PTS), menu_contents, ClientUI::TEXT_COLOR);

  if(popup.Run()) 
    switch (popup.MenuID())
    {
      case 1: 
      { // rename planet
        std::string plt_name = planet->Name();
        CUIEditWnd edit_wnd(350, UserString("SP_ENTER_NEW_PLANET_NAME"), plt_name);
        edit_wnd.Run();
        if(edit_wnd.Result() != "")
        {
          HumanClientApp::Orders().IssueOrder(new RenameOrder(HumanClientApp::GetApp()->EmpireID(), planet->ID(), edit_wnd.Result()));
          m_planet_name->SetText(planet->Name());
        }
        break;
      }
      default:
        break;
    }
}

 ////////////////////////////////////////////////
// SidePanel::PlanetPanelContainer
////////////////////////////////////////////////
SidePanel::PlanetPanelContainer::PlanetPanelContainer(int x, int y, int w, int h)
: Wnd(x-MAX_PLANET_DIAMETER/2, y, w+MAX_PLANET_DIAMETER/2, h, GG::Wnd::CLICKABLE),m_planet_panels(),
  m_vscroll(new CUIScroll(Width()-10,0,10,Height(),GG::Scroll::VERTICAL))
{
  EnableChildClipping(true);
  AttachChild(m_vscroll);
  Connect(m_vscroll->ScrolledSignal(), &SidePanel::PlanetPanelContainer::VScroll,this);
}

bool SidePanel::PlanetPanelContainer::InWindow(const GG::Pt& pt) const
{
  if(pt.y<UpperLeft().y)
    return false;

  bool retval = UpperLeft()+GG::Pt(MAX_PLANET_DIAMETER/2,0) <= pt && pt < LowerRight();
  for(unsigned int i = 0; i < m_planet_panels.size() && !retval; ++i)
    if(m_planet_panels[i]->InWindow(pt))
      retval = true;

  return retval;
}
void SidePanel::PlanetPanelContainer::MouseWheel(const GG::Pt& pt, int move, Uint32 keys)
{
  if(m_vscroll)
    move<0?m_vscroll->ScrollLineIncr():m_vscroll->ScrollLineDecr();
}

void SidePanel::PlanetPanelContainer::Clear()
{
  m_planet_panels.clear();

  DetachChild(m_vscroll);
  DeleteChildren();
  AttachChild(m_vscroll);
}

void SidePanel::PlanetPanelContainer::SetPlanets(const std::vector<const Planet*> &plt_vec, StarType star_type)
{
  Clear();

  int y = 0;
  const int PLANET_PANEL_HT = MAX_PLANET_DIAMETER;
  for (unsigned int i = 0; i < plt_vec.size(); ++i, y += PLANET_PANEL_HT) 
  {
    const Planet* planet = plt_vec[i];
    PlanetPanel* planet_panel = new PlanetPanel(0, y, Width()-m_vscroll->Width(), PLANET_PANEL_HT, *planet, star_type);
    AttachChild(planet_panel);
    m_planet_panels.push_back(planet_panel);
  }
  m_vscroll->SizeScroll(0,plt_vec.size()*PLANET_PANEL_HT,PLANET_PANEL_HT,Height());
  VScroll(m_vscroll->PosnRange().first, 0, 0, 0);
}

void SidePanel::PlanetPanelContainer::VScroll(int from,int to,int range_min,int range_max)
{
  int y = -from;
  const int PLANET_PANEL_HT = MAX_PLANET_DIAMETER;
  for (unsigned int i = 0; i < m_planet_panels.size(); ++i, y += PLANET_PANEL_HT)
    m_planet_panels[i]->MoveTo(UpperLeft().x-m_planet_panels[i]->UpperLeft().x,y);
}

////////////////////////////////////////////////
// SidePanel::SystemResourceSummary
////////////////////////////////////////////////
SidePanel::SystemResourceSummary::SystemResourceSummary(int x, int y, int w, int h)
: Wnd(x, y, w, h, GG::Wnd::CLICKABLE),
  m_farming(0),m_mining(0),m_research(0),m_industry(0),m_defense(0)
{
}

bool SidePanel::SystemResourceSummary::Render()
{
  GG::FlatRectangle(UpperLeft().x,UpperLeft().y,LowerRight().x,LowerRight().y,GG::Clr(0.0,0.0,0.0,0.5),GG::CLR_ZERO,1);

  int farming=m_farming,mining=m_mining,research=m_research,industry=m_industry,defense=m_defense;

  std::string text; int x,y; boost::shared_ptr<GG::Texture> icon;
  boost::shared_ptr<GG::Font> font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*1.2));
  Uint32 format = GG::TF_LEFT | GG::TF_VCENTER;
  const int ICON_MARGIN    =  5;
  
  x=UpperLeft().x;y=UpperLeft().y;

  int info_elem_width = (Width()-(5+1)*ICON_MARGIN)/5;

  //farming
  glColor4ubv(ClientUI::TEXT_COLOR.v);
  icon=IconFarming(); icon->OrthoBlit(x,y,x+font->Height(),y+font->Height(), 0, false);
  //x+=font->Height();
  text = (farming<0?"-":"+") + lexical_cast<std::string>(farming);
  font->RenderText(x+font->Height(),y,x + 500, y+Height(), text, format, 0);
  //x+=font->TextExtent(text, format).x+ICON_MARGIN;
  x+=info_elem_width+ICON_MARGIN;

  //mining
  glColor4ubv(ClientUI::TEXT_COLOR.v);
  icon=IconMining(); icon->OrthoBlit(x,y,x+font->Height(),y+font->Height(), 0, false);
  //x+=font->Height();
  text = (mining<0?"-":"+") + lexical_cast<std::string>(mining);
  font->RenderText(x+font->Height(),y,x + 500, y+Height(), text, format, 0);
  //x+=font->TextExtent(text, format).x+ICON_MARGIN;
  x+=info_elem_width+ICON_MARGIN;

  //research
  glColor4ubv(ClientUI::TEXT_COLOR.v);
  icon=IconResearch(); icon->OrthoBlit(x,y,x+font->Height(),y+font->Height(), 0, false);
  //x+=font->Height();
  text = (research<0?"-":"+") + lexical_cast<std::string>(research);
  font->RenderText(x+font->Height(),y,x + 500, y+Height(), text, format, 0);
  //x+=font->TextExtent(text, format).x+ICON_MARGIN;
  x+=info_elem_width+ICON_MARGIN;

  //industy
  glColor4ubv(ClientUI::TEXT_COLOR.v);
  icon=IconIndustry(); icon->OrthoBlit(x,y,x+font->Height(),y+font->Height(), 0, false);
  //x+=font->Height();
  text = (industry<0?"-":"+") + lexical_cast<std::string>(industry);
  font->RenderText(x+font->Height(),y,x + 500, y+Height(), text, format, 0);
  //x+=font->TextExtent(text, format).x+ICON_MARGIN;
  x+=info_elem_width+ICON_MARGIN;

  //defense
  glColor4ubv(ClientUI::TEXT_COLOR.v);
  icon=IconDefense(); icon->OrthoBlit(x,y,x+font->Height(),y+font->Height(), 0, false);
  //x+=font->Height();
  text = lexical_cast<std::string>(defense)+"/"+lexical_cast<std::string>(defense*3);
  font->RenderText(x+font->Height(),y,x + 500, y+Height(), text, format, 0);
  //x+=font->TextExtent(text, format).x+ICON_MARGIN;
  x+=info_elem_width+ICON_MARGIN;

  return true;
}

////////////////////////////////////////////////
// SidePanel::PlanetView
////////////////////////////////////////////////
SidePanel::PlanetView::PlanetView(int x, int y, int w, int h,const Planet &plt)
: Wnd(x, y, w, h, GG::Wnd::CLICKABLE | GG::Wnd::ONTOP),
  m_planet_id(plt.ID()),
  m_bShowUI(false),
  m_fadein_start(0),m_fadein_span(0),
  m_bg_image(),
  m_radio_btn_primary_focus  (0),
  m_radio_btn_secondary_focus(0)
{
  TempUISoundDisabler sound_disabler;

  Planet *planet = GetUniverse().Object<Planet>(m_planet_id);

  EnableChildClipping(true);

  boost::shared_ptr<GG::Texture> texture;
  switch(planet->Type())
  {
    case PT_SWAMP     : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/swamp1.png");break;
    case PT_TOXIC     : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/toxic1.png");break;
    case PT_INFERNO   : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/inferno1.png");break;
    case PT_RADIATED  : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/irradiated1.png");break;
    case PT_BARREN    : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/barren1.png");break;
    case PT_TUNDRA    : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/tundra1.png");break;
    case PT_DESERT    : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/desert1.png");break;
    case PT_TERRAN    : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/terran1.png");break;
    case PT_OCEAN     : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/ocean1.png");break;
    case PT_GAIA      : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/terran1.png");break;
    case PT_ASTEROIDS : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/terran1.png");break;
    case PT_GASGIANT  : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/terran1.png");break;       
    default           : texture=GetTexture(ClientUI::ART_DIR + "planets_bg/terran1.png");break;
  }
  m_bg_image = GG::SubTexture(texture,0,0,texture->DefaultWidth(),texture->DefaultHeight());

  texture=GetTexture(ClientUI::ART_DIR + "misc/planetpanelpictures.png");
  m_foci_image = GG::SubTexture(texture,0,0,texture->DefaultWidth(),texture->DefaultHeight());

  GG::Pt ul = UpperLeft();

  m_btn_fullscreen = new CUITextureButton(20,70-15,15,15,GetTexture(ClientUI::ART_DIR + "icons/fullscreenbutton.png"),GG::CLR_ZERO,GG::CLR_ZERO);
  AttachChild(m_btn_fullscreen);

  m_radio_btn_primary_focus = new GG::RadioButtonGroup(20,145);
  m_radio_btn_primary_focus->AddButton(new CUIStateButton(0,  0,10,10,"",0,CUIStateButton::SBSTYLE_CUI_RADIO_BUTTON,GG::CLR_WHITE));
  m_radio_btn_primary_focus->AddButton(new CUIStateButton(0, 35,10,10,"",0,CUIStateButton::SBSTYLE_CUI_RADIO_BUTTON,GG::CLR_WHITE));
  m_radio_btn_primary_focus->AddButton(new CUIStateButton(0, 70,10,10,"",0,CUIStateButton::SBSTYLE_CUI_RADIO_BUTTON,GG::CLR_WHITE));
  m_radio_btn_primary_focus->AddButton(new CUIStateButton(0,105,10,10,"",0,CUIStateButton::SBSTYLE_CUI_RADIO_BUTTON,GG::CLR_WHITE));
  m_radio_btn_primary_focus->AddButton(new CUIStateButton(0,130,10,10,"",0,CUIStateButton::SBSTYLE_CUI_RADIO_BUTTON,GG::CLR_WHITE));
  AttachChild(m_radio_btn_primary_focus);

  m_radio_btn_secondary_focus = new GG::RadioButtonGroup(40,145);
  m_radio_btn_secondary_focus->AddButton(new CUIStateButton(0,  0,10,10,"",0,CUIStateButton::SBSTYLE_CUI_RADIO_BUTTON,GG::CLR_WHITE));
  m_radio_btn_secondary_focus->AddButton(new CUIStateButton(0, 35,10,10,"",0,CUIStateButton::SBSTYLE_CUI_RADIO_BUTTON,GG::CLR_WHITE));
  m_radio_btn_secondary_focus->AddButton(new CUIStateButton(0, 70,10,10,"",0,CUIStateButton::SBSTYLE_CUI_RADIO_BUTTON,GG::CLR_WHITE));
  m_radio_btn_secondary_focus->AddButton(new CUIStateButton(0,105,10,10,"",0,CUIStateButton::SBSTYLE_CUI_RADIO_BUTTON,GG::CLR_WHITE));
  m_radio_btn_secondary_focus->AddButton(new CUIStateButton(0,130,10,10,"",0,CUIStateButton::SBSTYLE_CUI_RADIO_BUTTON,GG::CLR_WHITE));
  AttachChild(m_radio_btn_secondary_focus);

  GG::Connect(planet->StateChangedSignal(), &SidePanel::PlanetView::PlanetChanged, this);
  m_connection_planet_production_changed=GG::Connect(planet->ResourceCenterChangedSignal(), &SidePanel::PlanetView::PlanetResourceCenterChanged, this);
  PlanetChanged();
  PlanetResourceCenterChanged();
  m_connection_btn_primary_focus_changed = GG::Connect(m_radio_btn_primary_focus->ButtonChangedSignal(), &SidePanel::PlanetView::PrimaryFocusClicked, this);
  m_connection_btn_secondary_focus_changed = GG::Connect(m_radio_btn_secondary_focus->ButtonChangedSignal(), &SidePanel::PlanetView::SecondaryFocusClicked, this);
 }

void SidePanel::PlanetView::PlanetChanged()
{
  Planet *planet = GetUniverse().Object<Planet>(m_planet_id);
  if(planet==0)
    throw std::runtime_error("SidePanel::PlanetView::PlanetChanged: planet not found");

  bool is_owner = planet->OwnedBy(HumanClientApp::GetApp()->EmpireID());

  is_owner?m_radio_btn_primary_focus->Show():m_radio_btn_primary_focus->Hide();
  is_owner?m_radio_btn_secondary_focus->Show():m_radio_btn_secondary_focus->Hide();
}

void SidePanel::PlanetView::PlanetResourceCenterChanged()
{
  TempUISoundDisabler soud_disabler;
  Planet *planet = GetUniverse().Object<Planet>(m_planet_id);
  if(planet==0)
    throw std::runtime_error("SidePanel::PlanetView::PlanetChanged: planet not found");

  switch(planet->PrimaryFocus())
  {
    case FOCUS_FARMING  : m_radio_btn_primary_focus->SetCheck(0);break;
    case FOCUS_MINING   : m_radio_btn_primary_focus->SetCheck(1);break;
    case FOCUS_RESEARCH : m_radio_btn_primary_focus->SetCheck(2);break;
    case FOCUS_INDUSTRY : m_radio_btn_primary_focus->SetCheck(3);break;
    case FOCUS_BALANCED : m_radio_btn_primary_focus->SetCheck(4);break;
    default: break;
  }
  switch(planet->SecondaryFocus())
  {
    case FOCUS_FARMING  : m_radio_btn_secondary_focus->SetCheck(0);break;
    case FOCUS_MINING   : m_radio_btn_secondary_focus->SetCheck(1);break;
    case FOCUS_RESEARCH : m_radio_btn_secondary_focus->SetCheck(2);break;
    case FOCUS_INDUSTRY : m_radio_btn_secondary_focus->SetCheck(3);break;
    case FOCUS_BALANCED : m_radio_btn_secondary_focus->SetCheck(4);break;
    default: break;
  }
}

void SidePanel::PlanetView::Show(bool children)
{
  GG::Wnd::Show(children);

  const Planet *planet = GetUniverse().Object<const Planet>(m_planet_id);
  if(!planet)
    throw std::runtime_error("SidePanel::PlanetView::Show: planet not found!");
  enum OWNERSHIP {OS_NONE,OS_FOREIGN,OS_SELF} owner = OS_NONE;

  std::string text;
  if(planet->Owners().size()==0) 
    owner = OS_NONE;
  else 
    if(!planet->OwnedBy(HumanClientApp::GetApp()->EmpireID()))
      owner = OS_FOREIGN;
    else
      owner = OS_SELF;

  // visibility
  (m_bShowUI && owner==OS_SELF)?m_radio_btn_primary_focus  ->Show():m_radio_btn_primary_focus  ->Hide();
  (m_bShowUI && owner==OS_SELF)?m_radio_btn_secondary_focus->Show():m_radio_btn_secondary_focus->Hide();
}

void SidePanel::PlanetView::FadeIn()
{
  double fade_in = static_cast<double>((GG::App::GetApp()->Ticks()-m_fadein_start))/static_cast<double>(m_fadein_span);

  if(fade_in>=1.0)
  {
    m_transparency=255;

    if(!m_bShowUI)
    {
      m_bShowUI = true;
      if(Visible())
        Show();
    }
  }
  else
    m_transparency = static_cast<int>(fade_in*255);
}

void SidePanel::PlanetView::SetFadeInPlanetView  (int start, int span)
{
  m_bShowUI=false;
  m_fadein_start=start;m_fadein_span=span;

  m_transparency = 0;

  if(Visible())
    Show();
}

void SidePanel::PlanetView::SetFadeInPlanetViewUI(int start, int span)
{
  m_bShowUI=true;
  m_fadein_start=start;m_fadein_span=span;

  m_transparency = 255;

  if(Visible())
    Show();
}

void SidePanel::PlanetView::PrimaryFocusClicked(int idx)
{
  m_connection_planet_production_changed.disconnect();
  m_connection_btn_primary_focus_changed.disconnect();

  FocusType ft=FOCUS_UNKNOWN;
  switch(idx)
  {
    case  0:ft=FOCUS_FARMING      ;break;
    case  1:ft=FOCUS_MINING       ;break;
    case  2:ft=FOCUS_RESEARCH     ;break;
    case  3:ft=FOCUS_INDUSTRY     ;break;
    case  4:ft=FOCUS_BALANCED     ;break;
    default:ft=FOCUS_UNKNOWN;break;
  }
  Planet *planet = GetUniverse().Object<Planet>(m_planet_id);
  if(planet->PrimaryFocus()!=ft)
    HumanClientApp::Orders().IssueOrder(new ChangeFocusOrder(HumanClientApp::GetApp()->EmpireID(),planet->ID(),ft,true));
 
  m_connection_btn_primary_focus_changed = GG::Connect(m_radio_btn_primary_focus->ButtonChangedSignal(), &SidePanel::PlanetView::PrimaryFocusClicked, this, boost::signals::at_front);
  m_connection_planet_production_changed=GG::Connect(planet->ResourceCenterChangedSignal(), &SidePanel::PlanetView::PlanetResourceCenterChanged, this, boost::signals::at_front);
}

void SidePanel::PlanetView::SecondaryFocusClicked(int idx)
{
  m_connection_planet_production_changed.disconnect();
  m_connection_btn_secondary_focus_changed.disconnect();

  FocusType ft=FOCUS_UNKNOWN;
  switch(idx)
  {
    case  0:ft=FOCUS_FARMING      ;break;
    case  1:ft=FOCUS_MINING       ;break;
    case  2:ft=FOCUS_RESEARCH     ;break;
    case  3:ft=FOCUS_INDUSTRY     ;break;
    case  4:ft=FOCUS_BALANCED     ;break;
    default:ft=FOCUS_UNKNOWN;break;
  }
  Planet *planet = GetUniverse().Object<Planet>(m_planet_id);
  if(planet->SecondaryFocus()!=ft)
    HumanClientApp::Orders().IssueOrder(new ChangeFocusOrder(HumanClientApp::GetApp()->EmpireID(),planet->ID(),ft,false));

  m_connection_btn_secondary_focus_changed = GG::Connect(m_radio_btn_secondary_focus->ButtonChangedSignal(), &SidePanel::PlanetView::SecondaryFocusClicked, this, boost::signals::at_front);
  m_connection_planet_production_changed=GG::Connect(planet->ResourceCenterChangedSignal(), &SidePanel::PlanetView::PlanetResourceCenterChanged, this, boost::signals::at_front);
}

bool SidePanel::PlanetView::Render()
{
  FadeIn();

  const Planet *planet = GetUniverse().Object<const Planet>(m_planet_id);
  if(!planet)
    throw std::runtime_error("SidePanel::PlanetView::Render planet not found!");

  GG::Pt ul = UpperLeft(), lr = LowerRight();
  GG::Pt client_ul = ClientUpperLeft(), client_lr = ClientLowerRight();

  GG::BeginScissorClipping(ClientUpperLeft(), ClientLowerRight());
  int height = (Width() * m_bg_image.Height()) / m_bg_image.Width();
  
  GG::Clr border_color=GG::CLR_WHITE;

  if(!m_bShowUI)
  {
    glColor4ubv(GG::Clr(m_transparency,m_transparency,m_transparency,255).v);
    border_color = GG::Clr(m_transparency,m_transparency,m_transparency,255);
  }

  m_bg_image.OrthoBlit(ul.x,ul.y+(Height()-height)/2,lr.x,ul.y+(Height()-height)/2+height,false);
  GG::EndScissorClipping();
  
  AngledCornerRectangle(ul.x-2,ul.y-2,lr.x+2, lr.y+2, GG::CLR_ZERO, GG::Clr(200,200,200,255), 1,0,0,0,0);
  AngledCornerRectangle(ul.x-1,ul.y-1,lr.x+1, lr.y+1, GG::CLR_ZERO, GG::Clr(125,125,125,255), 1,0,0,0,0);

  if(!m_bShowUI)
    return true;

  if(!planet->OwnedBy(HumanClientApp::GetApp()->EmpireID()))    
    return true;

  GG::Clr alpha_color(GG::CLR_WHITE);
  if(m_bShowUI)
  {
    alpha_color=GG::Clr(m_transparency,m_transparency,m_transparency,255).v;
  }

  AngledCornerRectangle(ul.x+10, ul.y+35,ul.x+244, ul.y+ 70, GG::Clr(0.0,0.0,0.0,0.3), GG::CLR_ZERO,0,0,0,0,0);
  AngledCornerRectangle(ul.x+10, ul.y+70,ul.x+244, ul.y+290, GG::Clr(0.0,0.0,0.0,0.6), GG::CLR_ZERO,0,0,0,0,0);
  glColor4ubv(alpha_color.v);

  m_foci_image.OrthoBlit(ul+GG::Pt(65,130),ul+GG::Pt(85+m_foci_image.Width(),140+130),false);

  boost::shared_ptr<GG::Font> font;std::string text; int y;
  Uint32 format = GG::TF_LEFT | GG::TF_VCENTER;

  glColor4ubv(alpha_color.v);

  y = ul.y+40;
  text = planet->Name();
  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT_BOLD, static_cast<int>(ClientUI::SIDE_PANEL_PTS*1.3));
  font->RenderText(ul.x+40,y,ul.x+500,y+font->Height(), text, format, 0);
  y+=font->Height();

  text = GetPlanetSizeName(*planet);if(text.length()>0) text+=" "; text+= GetPlanetTypeName(*planet);
  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*1.0));
  font->RenderText(ul.x+40,y-2,ul.x+500,y-2+font->Height(), text, format, 0);
  y+=font->Height();

  y = ul.y+80;
  text = "Population";
  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*1.0));
  font->RenderText(ul.x+20,y,ul.x+500,y+font->Height(), text, format, 0);

  if(planet->MaxPop()==0) text= UserString("PE_UNINHABITABLE");
  else                    text= " "+lexical_cast<std::string>(static_cast<int>(planet->PopPoints()))+"/"+lexical_cast<std::string>(planet->MaxPop()) + " Million";

  text+="  ";

  double future_pop_growth = static_cast<int>(planet->FuturePopGrowth()*100.0) / 100.0;
  if     (future_pop_growth<0.0)  text+=GG::RgbaTag(GG::CLR_RED) + "(";
  else if(future_pop_growth>0.0)  text+=GG::RgbaTag(GG::CLR_GREEN) + "(+";
       else                       text+=GG::RgbaTag(ClientUI::TEXT_COLOR) + "(";

  text+=lexical_cast<std::string>(future_pop_growth);
  text+=")</rgba>";

  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*1.0));
  font->RenderText(ul.x+20+80,y,ul.x+500,y+font->Height(), text, format, 0);
  y+=font->Height();


  text = "Immigration";
  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*0.9));
  font->RenderText(ul.x+20,y,ul.x+500,y+font->Height(), text, format, 0);
  y+=font->Height();

  y+= 5;

  text = "Focus";
  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*1.0));
  font->RenderText(ul.x+20,y,ul.x+500,y+font->Height(), text, format, 0);
  
  text = "Production";
  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*1.0));
  font->RenderText(ul.x+175,y,ul.x+500,y+font->Height(), text, format, 0);
  y+=font->Height();

  text = "pri";
  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*0.9));
  font->RenderText(ul.x+20,y,ul.x+500,y+font->Height(), text, format, 0);

  text = "sec";
  font->RenderText(ul.x+40,y,ul.x+500,y+font->Height(), text, format, 0);
  
  y+=font->Height();

  text = "FOCUS_BALANCED Focus";
  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*1.0));
  font->RenderText(ul.x+65,ul.y+145+128,ul.x+500,ul.y+145+128+font->Height(), text, format, 0);
  y+=font->Height();


  font = HumanClientApp::GetApp()->GetFont(ClientUI::FONT, static_cast<int>(ClientUI::SIDE_PANEL_PTS*1.0));

  int farming=0,mining=0,research=0,industry=0,population=0;

  farming   +=static_cast<int>(planet->FarmingPoints());
  industry  +=static_cast<int>(planet->IndustryPoints());
  mining    +=static_cast<int>(planet->MiningPoints());
  research  +=static_cast<int>(planet->ResearchPoints());
  //defense   +=;
  population+=static_cast<int>(planet->PopPoints());

  int x,icon_dim = static_cast<int>(font->Height()*1.5);  boost::shared_ptr<GG::Texture> icon;

  format = GG::TF_RIGHT | GG::TF_VCENTER;
  x = UpperLeft().x+175;
  //farming
  y = UpperLeft().y+140;
  icon=IconFarming(); icon->OrthoBlit(x,y,x+icon_dim,y+icon_dim, 0, false);
  text = (farming<0?"-":"+") + lexical_cast<std::string>(farming);
  font->RenderText(x+icon_dim,y,x+icon_dim+35, y+icon_dim, text, format, 0);

  //mining
  y = UpperLeft().y+140+35;
  icon=IconMining(); icon->OrthoBlit(x,y,x+icon_dim,y+icon_dim, 0, false);
  text = (mining<0?"-":"+") + lexical_cast<std::string>(mining);
  font->RenderText(x+icon_dim,y,x+icon_dim+35, y+icon_dim, text, format, 0);

  //research
  y = UpperLeft().y+140+70;
  icon=IconResearch(); icon->OrthoBlit(x,y,x+icon_dim,y+icon_dim, 0, false);
  text = (research<0?"-":"+") + lexical_cast<std::string>(research);
  font->RenderText(x+icon_dim,y,x+icon_dim+35, y+icon_dim, text, format, 0);

  //industy
  y = UpperLeft().y+140+105;
  icon=IconIndustry(); icon->OrthoBlit(x,y,x+icon_dim,y+icon_dim, 0, false);
  text = (industry<0?"-":"+") + lexical_cast<std::string>(industry);
  font->RenderText(x+icon_dim,y,x+icon_dim+35, y+icon_dim, text, format, 0);

  AngledCornerRectangle(ul.x+255, ul.y+10,ul.x+489, ul.y+289, GG::Clr(0.0,0.0,0.0,0.6), GG::CLR_ZERO,0,0,0,0,0);

  return true;
}

////////////////////////////////////////////////
// SidePanel
////////////////////////////////////////////////
SidePanel::SidePanel(int x, int y, int w, int h) : 
    Wnd(x, y, w, h, GG::Wnd::CLICKABLE),
    m_system(0),
    m_system_name(new CUIDropDownList(40, 0, w-80,SYSTEM_NAME_FONT_SIZE, 10*SYSTEM_NAME_FONT_SIZE,GG::CLR_ZERO,GG::Clr(0.0, 0.0, 0.0, 0.5),ClientUI::SIDE_PANEL_COLOR)),
    m_system_name_unknown(new GG::TextControl(40, 0, w-80,SYSTEM_NAME_FONT_SIZE,UserString("SP_UNKNOWN_SYSTEM"),ClientUI::FONT,static_cast<int>(ClientUI::PTS*1.4),ClientUI::TEXT_COLOR)),
    m_button_prev(new GG::Button(40-SYSTEM_NAME_FONT_SIZE,4,SYSTEM_NAME_FONT_SIZE,SYSTEM_NAME_FONT_SIZE,"",ClientUI::FONT,SYSTEM_NAME_FONT_SIZE,GG::CLR_WHITE,GG::Wnd::CLICKABLE)),
    m_button_next(new GG::Button(40+w-80                 ,4,SYSTEM_NAME_FONT_SIZE,SYSTEM_NAME_FONT_SIZE,"",ClientUI::FONT,SYSTEM_NAME_FONT_SIZE,GG::CLR_WHITE,GG::Wnd::CLICKABLE)),
    m_star_graphic(0),
    m_static_text_systemproduction(new GG::TextControl(0,100-20-ClientUI::PTS-5,UserString("SP_SYSTEM_PRODUCTION"),ClientUI::FONT,ClientUI::PTS,ClientUI::TEXT_COLOR)),
    m_next_pltview_fade_in(0),m_next_pltview_planet_id(UniverseObject::INVALID_OBJECT_ID),m_next_pltview_fade_out(-1),
    m_planet_view(0),
    m_planet_panel_container(new PlanetPanelContainer(0,100,w,h-100-30)),
    m_system_resource_summary(new SystemResourceSummary(0,100-20,w,20))
{
  TempUISoundDisabler sound_disabler;

  SetText(UserString("SIDE_PANEL"));

  m_system_name->DisableDropArrow();
  m_system_name->SetStyle(GG::LB_CENTER);

  m_button_prev->SetUnpressedGraphic(GG::SubTexture(GetTexture( ClientUI::ART_DIR + "icons/leftarrownormal.png"   ), 0, 0, 32, 32));
  m_button_prev->SetPressedGraphic  (GG::SubTexture(GetTexture( ClientUI::ART_DIR + "icons/leftarrowclicked.png"  ), 0, 0, 32, 32));
  m_button_prev->SetRolloverGraphic (GG::SubTexture(GetTexture( ClientUI::ART_DIR + "icons/leftarrowmouseover.png"), 0, 0, 32, 32));

  m_button_next->SetUnpressedGraphic(GG::SubTexture(GetTexture( ClientUI::ART_DIR + "icons/rightarrownormal.png"  ), 0, 0, 32, 32));
  m_button_next->SetPressedGraphic  (GG::SubTexture(GetTexture( ClientUI::ART_DIR + "icons/rightarrowclicked.png"   ), 0, 0, 32, 32));
  m_button_next->SetRolloverGraphic (GG::SubTexture(GetTexture( ClientUI::ART_DIR + "icons/rightarrowmouseover.png"), 0, 0, 32, 32));

  AttachChild(m_system_name_unknown);
  AttachChild(m_system_name);
  AttachChild(m_button_prev);
  AttachChild(m_button_next);
  AttachChild(m_static_text_systemproduction);
  AttachChild(m_system_resource_summary);
  AttachChild(m_planet_panel_container);

  GG::Connect(m_system_name->SelChangedSignal(), &SidePanel::SystemSelectionChanged, this);
  GG::Connect(m_button_prev->ClickedSignal(), &SidePanel::PrevButtonClicked, this);
  GG::Connect(m_button_next->ClickedSignal(), &SidePanel::NextButtonClicked, this);

  Hide();
}

bool SidePanel::InWindow(const GG::Pt& pt) const
{
  return (UpperLeft() <= pt && pt < LowerRight()) || m_planet_panel_container->InWindow(pt);
}

bool SidePanel::Render()
{
  PlanetViewFadeIn();

  GG::Pt ul = UpperLeft(), lr = LowerRight();
  FlatRectangle(ul.x, ul.y, lr.x, lr.y, ClientUI::SIDE_PANEL_COLOR, GG::CLR_ZERO, 0);

  return true;
}

void SidePanel::SystemSelectionChanged(int selection)
{
  int system_id = UniverseObject::INVALID_OBJECT_ID;

  if(0<= selection && selection<m_system_name->NumRows())
    system_id = static_cast<const SystemRow&>(m_system_name->GetRow(selection)).m_system_id;

  if(SystemID()!=system_id)
    SetSystem(system_id);
}

void SidePanel::PrevButtonClicked()
{
  int selected = m_system_name->CurrentItemIndex();

  if(0< selected && selected<m_system_name->NumRows())
    m_system_name->Select(selected-1);
}

void SidePanel::NextButtonClicked()
{
  int selected = m_system_name->CurrentItemIndex();

  if(0<=selected && selected<m_system_name->NumRows()-1)
    m_system_name->Select(selected+1);
}

int SidePanel::SystemID() const {return m_system!=0?m_system->ID():UniverseObject::INVALID_OBJECT_ID;}

void SidePanel::SetSystem(int system_id)
{
    const System* new_system = HumanClientApp::GetUniverse().Object<const System>(system_id);
    if (new_system && new_system != m_system)
        PlaySidePanelOpenSound();
    TempUISoundDisabler sound_disabler;

    delete m_planet_view;m_planet_view=0;
    m_fleet_icons.clear();
    m_planet_panel_container->Clear();
    m_system_name->Clear();

    DeleteChild(m_star_graphic);m_star_graphic=0;

    Hide();

    m_system = new_system;

    if (m_system)
    {
      GG::Connect(m_system->FleetAddedSignal  (), &SidePanel::SystemFleetAdded  , this);
      GG::Connect(m_system->FleetRemovedSignal(), &SidePanel::SystemFleetRemoved, this);

      std::vector<const System*> sys_vec = GetUniverse().FindObjects<const System>();
      GG::ListBox::Row *select_row=0;

      for (unsigned int i = 0; i < sys_vec.size(); i++) 
      {
        GG::ListBox::Row *row = new SystemRow(sys_vec[i]->ID());

        if(sys_vec[i]->Name().length()==0)
          continue;
 
        row->push_back(boost::io::str(boost::format(UserString("SP_SYSTEM_NAME")) % sys_vec[i]->Name()), ClientUI::FONT,static_cast<int>(ClientUI::PTS*1.4), ClientUI::TEXT_COLOR);
        m_system_name->Insert(row);

        if(sys_vec[i]->ID() == system_id)
          select_row = row;
      }

      for (int i = 0; i < m_system_name->NumRows(); i++) 
        if(select_row == &m_system_name->GetRow(i))
        {
          m_system_name->Select(i);
          break;
        }

      std::vector<boost::shared_ptr<GG::Texture> > textures;
      boost::shared_ptr<GG::Texture> graphic;
     
      std::string star_image = ClientUI::ART_DIR + "stars_sidepanel/";
      switch (m_system->Star())
      {
        case STAR_BLUE     : star_image += "blue0"     ; break;
        case STAR_WHITE    : star_image += "white0"    ; break;
        case STAR_YELLOW   : star_image += "yellow0"   ; break;
        case STAR_ORANGE   : star_image += "orange0"   ; break;
        case STAR_RED      : star_image += "red0"      ; break;
        case STAR_NEUTRON  : star_image += "neutron0"  ; break;
        case STAR_BLACK    : star_image += "blackhole0"; break;
        default            : star_image += "white0"    ; break;
      }
      star_image += lexical_cast<std::string>(m_system->ID()%2)+".png";

      graphic = GetTexture(star_image);
      
      textures.push_back(graphic);

      //m_star_graphic = new GG::DynamicGraphic((Width()*2)/3,-(Width()*2)/3,Width(),Width(),true,textures.back()->Width(),textures.back()->Height(),0,textures, GG::GR_FITGRAPHIC | GG::GR_PROPSCALE);
      int star_dim = (Width()*4)/5;
      m_star_graphic = new GG::DynamicGraphic(Width()-(star_dim*2)/3,-(star_dim*1)/3,star_dim,star_dim,true,textures[0]->DefaultWidth(),textures[0]->DefaultHeight(),0,textures, GG::GR_FITGRAPHIC | GG::GR_PROPSCALE);

      AttachChild(m_star_graphic);MoveChildDown(m_star_graphic);

      // TODO: add fleet icons
      //std::vector<const Fleet*> flt_vec = m_system->FindObjects<Fleet>();
      std::pair<System::const_orbit_iterator, System::const_orbit_iterator> range = m_system->non_orbit_range();
      std::cout << "System " << m_system->ID() << ": " << m_system->Name() << " " << std::distance(range.first, range.second) << " objects {" << std::endl;
      for (System::const_orbit_iterator it = range.first; it != range.second; ++it) {
          UniverseObject* uo = GetUniverse().Object(it->second);
          std::cout << "    object " << it->second << " @" << uo << std::endl;
          if (uo)
              std::cout << "    object " << uo->ID() << ": " << uo->Name() << std::endl;
      }
      std::cout << "}" << std::endl;
      std::vector<const Fleet*> flt_vec = m_system->FindObjects<Fleet>();
      for(unsigned int i = 0; i < flt_vec.size(); i++) 
        GG::Connect(flt_vec[i]->StateChangedSignal(), &SidePanel::FleetsChanged, this);

      // add planets
      std::vector<const Planet*> plt_vec = m_system->FindObjects<Planet>();

      m_planet_panel_container->SetPlanets(plt_vec, m_system->Star());
      for(unsigned int i = 0; i < plt_vec.size(); i++) 
      {
        GG::Connect(plt_vec[i]->StateChangedSignal(), &SidePanel::PlanetsChanged, this);
        GG::Connect(plt_vec[i]->ResourceCenterChangedSignal(), &SidePanel::PlanetsChanged, this);
      }

      m_planet_panel_container->SetPlanets(plt_vec, m_system->Star());
      for(int i = 0; i < m_planet_panel_container->PlanetPanels(); i++) 
        GG::Connect(m_planet_panel_container->GetPlanetPanel(i)->PlanetImageLClickedSignal(),&SidePanel::PlanetLClicked,this);

      Show();PlanetsChanged();
      if(select_row==0)
      {
        m_system_name_unknown->Show();
        m_system_name->Hide();
        m_button_prev->Hide();
        m_button_next->Hide();
      }
      else
      {
        m_system_name_unknown->Hide();
        m_system_name->Show();
        m_button_prev->Show();
        m_button_next->Show();
      }
    }
}

void SidePanel::SystemFleetAdded  (const Fleet &flt)
{
  GG::Connect(flt.StateChangedSignal(), &SidePanel::FleetsChanged, this);
  FleetsChanged();
}

void SidePanel::SystemFleetRemoved(const Fleet &)
{
  FleetsChanged();
}

void SidePanel::FleetsChanged()
{
  for(int i=0;i<m_planet_panel_container->PlanetPanels();i++)
    m_planet_panel_container->GetPlanetPanel(i)->Update();
}

void SidePanel::PlanetsChanged()
{
  if(m_system)
  {
    std::vector<const Planet*> plt_vec = m_system->FindObjects<Planet>();
    int farming=0,mining=0,research=0,industry=0,defense=0,num_empire_planets=0;

    for(unsigned int i=0;i<plt_vec.size();i++)
      if(plt_vec[i]->Owners().find(HumanClientApp::GetApp()->EmpireID()) != plt_vec[i]->Owners().end())
      {
        farming   +=static_cast<int>(plt_vec[i]->FarmingPoints());
        industry  +=static_cast<int>(plt_vec[i]->IndustryPoints());
        mining    +=static_cast<int>(plt_vec[i]->MiningPoints());
        research  +=static_cast<int>(plt_vec[i]->ResearchPoints());
        defense   +=plt_vec[i]->DefBases();
        
        num_empire_planets++;
      }

    m_system_resource_summary->SetFarming (farming );
    m_system_resource_summary->SetMining  (mining  );
    m_system_resource_summary->SetResearch(research);
    m_system_resource_summary->SetIndustry(industry);
    m_system_resource_summary->SetDefense (defense );

    if(num_empire_planets==0)
    {
      m_system_resource_summary->Hide();
      m_static_text_systemproduction->Hide();
    }
    else
    {
      m_system_resource_summary->Show();
      m_static_text_systemproduction->Show();
    }
  }
}

void SidePanel::PlanetLClicked(int planet_id)
{
  if(   planet_id != UniverseObject::INVALID_OBJECT_ID
     && (!m_planet_view || m_planet_view->PlanetID()!=planet_id))
  {
    const Planet* planet = GetUniverse().Object<const Planet>(planet_id);

    if(m_planet_view)
    {
      GG::App::GetApp()->Remove(m_planet_view);
      delete m_planet_view;m_planet_view=0;
    }

    // don't show planetview for gas giants or asteriods fields
    if(planet->Type() == PT_ASTEROIDS || planet->Type() == PT_GASGIANT)
      return;

    int app_width = GetOptionsDB().Get<int>("app-width") - MapWnd::SIDE_PANEL_WIDTH, 
        app_height= GetOptionsDB().Get<int>("app-height");

    int pltview_width = 500, 
        pltview_height= 300;

    m_planet_view = new PlanetView((app_width-pltview_width)/2,(app_height-pltview_height)/2,pltview_width,pltview_height,*planet);
    m_planet_view->SetFadeInPlanetView(GG::App::GetApp()->Ticks(),/*40*/0);
    GG::App::GetApp()->Register(m_planet_view);

    m_next_pltview_planet_id=-1;
    m_next_pltview_fade_in=-1;
  }
}

void SidePanel::PlanetViewFadeIn()
{
  GG::Pt mouse_pos = GG::App::GetApp()->MousePosition();
  int plt_idx=-1; int planet_id=UniverseObject::INVALID_OBJECT_ID;
  
  // check if mouse is on top of a planet panel
  for(plt_idx=0;plt_idx<m_planet_panel_container->PlanetPanels();plt_idx++)
    if(m_planet_panel_container->GetPlanetPanel(plt_idx)->InWindow(mouse_pos))
    {
      planet_id = m_planet_panel_container->GetPlanetPanel(plt_idx)->PlanetID();
      break;
    }

  if(m_planet_view && m_planet_view->InWindow(mouse_pos))
    planet_id = m_planet_view->PlanetID();

  // set fadeout time index or cancel fade out
  if(!m_planet_view)
  {
    m_next_pltview_fade_out=-1;
  }
  else
    if(planet_id == m_planet_view->PlanetID())
    {
      m_next_pltview_fade_in =-1;
      m_next_pltview_fade_out=-1;
    }
    else
    {
      if(m_next_pltview_fade_out==-1)
        m_next_pltview_fade_out = GG::App::GetApp()->Ticks()+200;
    }

  if(m_next_pltview_fade_out!=-1 && m_next_pltview_fade_out<GG::App::GetApp()->Ticks())
  {
    GG::App::GetApp()->Remove(m_planet_view);
    delete m_planet_view;m_planet_view=0;
  }

// cancel fade in for now
return;

  if(   planet_id != UniverseObject::INVALID_OBJECT_ID
     && planet_id != m_next_pltview_planet_id
     && !(m_planet_view && m_planet_view->PlanetID()==planet_id))
  {
    m_next_pltview_planet_id = planet_id;
    m_next_pltview_fade_in = GG::App::GetApp()->Ticks()+1000;
  }

  if(   m_next_pltview_planet_id != UniverseObject::INVALID_OBJECT_ID 
     && m_next_pltview_fade_in!=-1 && m_next_pltview_fade_in < GG::App::GetApp()->Ticks())
  {
    if(m_planet_view)
    {
      GG::App::GetApp()->Remove(m_planet_view);
      delete m_planet_view;m_planet_view=0;
    }

    const Planet* planet = GetUniverse().Object<const Planet>(m_next_pltview_planet_id);

    int app_width = GetOptionsDB().Get<int>("app-width") - MapWnd::SIDE_PANEL_WIDTH, 
        app_height= GetOptionsDB().Get<int>("app-height");

    int pltview_width = 500, 
        pltview_height= 300;

    m_planet_view = new PlanetView((app_width-pltview_width)/2,(app_height-pltview_height)/2,pltview_width,pltview_height,*planet);
    m_planet_view->SetFadeInPlanetView(GG::App::GetApp()->Ticks(),400);
    GG::App::GetApp()->Register(m_planet_view);

    m_next_pltview_planet_id=-1;
    m_next_pltview_fade_in=-1;
  }
}
